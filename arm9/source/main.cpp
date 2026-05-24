#include <nds.h>
#include <stdio.h>
#include "gm9i/nandio.h"
#include <fat.h>
#include <stdarg.h>
#include <stdio.h>
#include <dirent.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "gm9i/crypto.h"
#include "gm9i/f_xy.h"
#include "polarssl/aes.h"
#include "twltool/dsi.h"
#include "u128_math.h"
#include "patch.h"
#include "patch_data.h"
#include "system_info.h"
#include "ui.h"

#define TARGETBUFFER 0x02900000

PrintConsole topScreen;
PrintConsole bottomScreen;
PrintConsole *currentScreen = &topScreen ;

void humanReadableByteSize(long size, char *buffer, int bufferLen)
{
	long full = abs(size), deci = 0 ;
	int exponent = 0 ;
	while ( full > 1024)
	{
		deci = (full * 10 / 1024) % 10 ;
		full = full / 1024 ;
		exponent++ ;
	}
	const char exponents[] = {' ', 'k', 'M', 'G', 'T'} ;
	snprintf(buffer, bufferLen, "%li.%u %cB", full, (u8)deci, exponents[exponent]) ;
}

typedef enum LOGLEVEL
{
	LOGLEVEL_ERROR,
	LOGLEVEL_WARNING,
	LOGLEVEL_INFO,
	LOGLEVEL_PROGRESS
} LOGLEVEL;

void Log(LOGLEVEL level, const char *format, ...)
{
  char buffer[256] ;
  consoleSelect(&topScreen);
	va_list ap, ap2;
	va_start(ap, format);
	va_copy(ap2, ap);
	vprintf(format, ap) ;
	va_end(ap);
	vsnprintf(buffer, sizeof(buffer), format, ap2) ;
	va_end(ap2);
  consoleSelect(&bottomScreen);

	if (level == LOGLEVEL_ERROR)
	{
		WaitForErrorRestart(buffer) ;
	}
}

typedef void (* progress_callback_t)(uint8_t percent) ;

void decrypt_modcrypt_area(dsi_context* ctx, u8 *buffer, unsigned int size, progress_callback_t callback)
{
	uint32_t len = size / 0x10;
	u8 block[0x10];

  uint32_t bytesPerPercent = (size + 1) / 101 ;
  uint8_t lastReportedProgress = (uint8_t)-1 ;
  uint32_t pos = 0 ;

	while(len>0)
	{
		memset(block, 0, 0x10);
		dsi_crypt_ctr_block(ctx, buffer, block);
		memcpy(buffer, block, 0x10);
		buffer+=0x10;
    pos += 0x10;
		len--;
    if (callback)
    {
      uint8_t progress = pos / bytesPerPercent ;
      if (lastReportedProgress != progress)
      {
        lastReportedProgress = progress ;
        callback(progress) ;
      }
    }
	}
}

// AES-CTR is its own inverse, so this same function handles both directions:
//   - called before patching  → decrypts the sections into plaintext
//   - called after patching   → re-encrypts the patched plaintext back
// The CTR values are read directly from the binary (0x300 for section 1,
// 0x314 for section 2) and are never modified, so both calls use the same inputs.
static void apply_modcrypt(uint8_t *target,
                           const u8 *key,
                           const uint32_t *offsets,
                           const uint32_t *lengths,
                           const char *msg1,
                           const char *msg2)
{
	dsi_context ctx;

	CreateProgress(msg1);
	dsi_set_key(&ctx, key);
	dsi_set_ctr(&ctx, &target[0x300]);
	if (lengths[0]) {
		decrypt_modcrypt_area(&ctx, target+offsets[0], lengths[0], &UpdateProgress) ;
	}	
	ClearProgress();

	CreateProgress(msg2);
	dsi_set_key(&ctx, key);
	dsi_set_ctr(&ctx, &target[0x314]);
	if (lengths[1]) {
		decrypt_modcrypt_area(&ctx, target+offsets[1], lengths[1], &UpdateProgress) ;
	}
	ClearProgress() ;
}

//---------------------------------------------------------------------------------
int main(void) {
//---------------------------------------------------------------------------------	
	videoSetMode(MODE_0_2D);
	videoSetModeSub(MODE_0_2D);

	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);

	consoleInit(&topScreen, 3,BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
	consoleInit(&bottomScreen, 3,BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);

	// picolibc buffers stdout by default; make it unbuffered so every printf
	// call immediately updates the tile buffer, matching the old iprintf behaviour.
	setvbuf(stdout, NULL, _IONBF, 0);

	consoleSelect(&topScreen);  
  InfoBorder() ;
  consoleSetWindow(&topScreen, 0, 2, 32, 22) ;
	consoleSelect(&bottomScreen);  
  InfoBorder() ;
	
	for (int i=0;i<30;i++)
		swiWaitForVBlank() ;
		
	Log(LOGLEVEL_INFO, "[i] CID:\n      ") ;
	u8 *CID = (u8 *)0x2FFD7BC ;
	for (int i=0;i<8;i++)
	{
		Log(LOGLEVEL_INFO, "%02X", CID[i]) ;
	}
	Log(LOGLEVEL_INFO, "\n      ") ;
	for (int i=8;i<16;i++)
	{
		Log(LOGLEVEL_INFO, "%02X", CID[i]) ;
	}
	Log(LOGLEVEL_INFO, "\n") ;
	
	Log(LOGLEVEL_INFO, "[i] ConsoleID:\n      ") ;
	u8 consoleID[8] ;
	getConsoleID(consoleID) ;
	for (int i=0;i<8;i++)
	{
		Log(LOGLEVEL_INFO, "%02X", consoleID[7-i]) ;
	}
	Log(LOGLEVEL_INFO, "\n") ;
	
	if(consoleID[7] != 0x08)
	{
		Log(LOGLEVEL_ERROR, "[E] Invalid ConsoleID found!\n");
	}

	// BlocksDS: fatInitDefault() must be called first to initialise the FAT
	// layer (DSi SD card + flashcard DLDI).  nandInit() then mounts the
	// encrypted DSi NAND on top of that.  Calling nandInit() without
	// fatInit() first causes it to hang waiting for ARM7 setup that never
	// completes.
	Log(LOGLEVEL_INFO, "[i] Calling fatInitDefault\n") ;
	if (!fatInitDefault())
	{
		Log(LOGLEVEL_ERROR, "[E] Could not init FAT\n");
	}
	Log(LOGLEVEL_INFO, "[i] Calling nandInit\n") ;
	if (!nandInit(false))
	{
		Log(LOGLEVEL_ERROR, "[E] Could not mount NAND\n");
	}
	Log(LOGLEVEL_INFO, "[i] NAND mounted\n") ;
	
	long nandSize = 0;
	struct statvfs st;
	if (statvfs("nand:/", &st) == 0) {
		nandSize = st.f_bsize * st.f_blocks;
	}

	char buffer[20] ;

	humanReadableByteSize(nandSize, buffer, sizeof(buffer)) ;
	Log(LOGLEVEL_INFO, "[i] NAND Size: %s\n", buffer);
	
	uint8_t launcherRegion = 0 ;
	
	char * appLauncherDirName = system_getLauncherPath(&launcherRegion);
	
	if (!appLauncherDirName)
	{
		Log(LOGLEVEL_ERROR, "[E] Could not find folder\n");
	}
	
	Log(LOGLEVEL_INFO, "[i] Launcher Region:\n      %s\n",  knownRegions[launcherRegion].name);
	if ( knownRegions[launcherRegion].code > 3)
	{
		Log(LOGLEVEL_ERROR, "[E] Launcher is KOR or CHN\n");
	}	
	
	char * appFileName = system_getAppFilename(appLauncherDirName) ;
  char * tmdFileName = system_getTmdFilename(appLauncherDirName) ;
  
  
	bool unlaunchInstalled = false ;
	
  // If that file is longer than 1k, unlaunch is appended
  // Todo: get version of unlaunch
  struct stat tmdInfo ;
  memset(&tmdInfo, 0, sizeof(tmdInfo)) ;	
  stat(tmdFileName, &tmdInfo) ;
  free(tmdFileName) ;
  unlaunchInstalled = (tmdInfo.st_size > 1024) ;
  
  
  if (unlaunchInstalled)
  {
    Log(LOGLEVEL_INFO, "[i] Unlaunch is installed\n") ;
  } else
  {
    Log(LOGLEVEL_INFO, "[i] Unlaunch is not installed\n") ;
  }

	if (!appFileName)
	{
		Log(LOGLEVEL_ERROR, "[E] Could not find app\n");
	}
	
  uint8_t *target = (uint8_t *)TARGETBUFFER ;
  
  CreateProgress("Reading Launcher") ;
	uint32_t appLauncherSize = system_readFile(target, appFileName, &UpdateProgress) ;
  ClearProgress() ;
	
	humanReadableByteSize(appLauncherSize, buffer, sizeof(buffer)) ;
	Log(LOGLEVEL_INFO, "[i] Launcher Size: %s\n", buffer);	


	if (!appLauncherSize)
	{
		Log(LOGLEVEL_ERROR, "[E] Could not read launcher\n");
	}
  
	// Hoisted outside the if-block so the re-encryption step after patching
	// can reuse the same key, offsets and lengths.
	bool doReencrypt = false ;
	u8 modcryptKey[16] = {0} ;
	uint32_t modcryptOffsets[2] = {0, 0} ;
	uint32_t modcryptLengths[2] = {0, 0} ;

	if (target[0x01C] & 2)
	{
    CreateProgress("Processing modcrypt") ;

		u8 keyp[16] = {0} ;
		if (target[0x01C] & 4)
		{
			// Debug Key
			memcpy(modcryptKey, target, 16) ;
		} else
		{
			// Retail key: derived from the shared Nintendo key + game-specific bytes
			char modcrypt_shared_key[8] = {'N','i','n','t','e','n','d','o'};
			memcpy(keyp, modcrypt_shared_key, 8) ;
			for (int i=0;i<4;i++)
			{
				keyp[8+i] = target[0x0c+i] ;
				keyp[15-i] = target[0x0c+i] ;
			}
			memcpy(modcryptKey, target+0x350, 16) ;

			u128_xor(modcryptKey, keyp);
			u128_add(modcryptKey, DSi_KEY_MAGIC);
			u128_lrot(modcryptKey, 42) ;
		}

		// Read section offsets and lengths from the NDS header at 0x220.
		// These are kept intact (not zeroed) so the binary stays valid for Unlaunch.
		modcryptOffsets[0] = ((uint32_t *)(target+0x220))[0] ;
		modcryptOffsets[1] = ((uint32_t *)(target+0x220))[2] ;
		modcryptLengths[0] = ((uint32_t *)(target+0x220))[1] ;
		modcryptLengths[1] = ((uint32_t *)(target+0x220))[3] ;

    // Decrypt both sections into plaintext so the pattern patches can find
    // and modify the getter functions inside them.
    apply_modcrypt(target, modcryptKey, modcryptOffsets, modcryptLengths,
                   "Decrypting modcrypt #1", "Decrypting modcrypt #2") ;

    // Signal that re-encryption is needed after patching.
    // Previously this block zeroed the descriptor at 0x220 and left the
    // modcrypt flag at 0x01C set, along with those sections unencrypted.
	// These additional changes, caused Unlaunch to attempt decryption on plaintext,
	// produce garbage, and enter an error loop.
    doReencrypt = true ;
	}

  SPATCHRESULT patchResults[] =
  {
    {0, 0},
    {0, 0},
    {0, 0}
  };  
  
  const uint32_t patchCount = sizeof(patchList) / sizeof(SPATCHLISTENTRY) ;

  CreateProgress("Applying patches") ;
  patch_applyPatternPatches(target, appLauncherSize,
                            patchList, patchCount, patchResults, &UpdateProgress) ;
  ClearProgress() ;

  for (uint32_t i=0;i<patchCount;i++)
  {
    if (patchResults[i].matchCount == 0)
    {
      Log(LOGLEVEL_ERROR, "[E] Pattern %s not found\n", patchList[i].name) ;
    }
    if (patchResults[i].matchCount > 1)
    {
      Log(LOGLEVEL_ERROR, "[E] Pattern %s too often\n", patchList[i].name) ;
    }
    Log(LOGLEVEL_INFO, "[i] Patch \'%s\' found\n", patchList[i].name) ;
  }
  
  Log(LOGLEVEL_PROGRESS, "[-] getting options\n") ;
  std::vector<SOPTIONSELECT> 
    options = patch_getAvailableOptions(patchList, patchCount) ;
    
  for (uint32_t i = 0;i<options.size();i++)
  {
    options[i].selection = OptionSelect(options[i].name, options[i].values, options[i].selection) ;
  }
  
  patch_applySelectedOptions(target, appLauncherSize,
                                patchList, patchResults, patchCount, 
                                options) ;              
  
  // Re-encrypt the patched plaintext back into valid modcrypt sections.
  // AES-CTR is its own inverse, so apply_modcrypt() with the same key and
  // CTR values re-encrypts exactly as it decrypted. After this the binary
  // looks like a normal launcher — valid encrypted sections, intact descriptor,
  // intact flag — and Unlaunch can decrypt and patch it without hitting an
  // error loop.
  if (doReencrypt)
  {
    apply_modcrypt(target, modcryptKey, modcryptOffsets, modcryptLengths,
                   "Re-encrypting modcrypt #1", "Re-encrypting modcrypt #2") ;
  }

	WaitForPowercord() ;
	
  if (!WaitForKonami("Write to internal NAND\n"
                "     CAUTION! Risk to brick!"))
  {
    Log(LOGLEVEL_ERROR, "[E] Failed to enter code\n") ;
  }

  // we will write the file back to NAND at root
  CreateProgress("Writing to NAND") ;
  uint32_t written = system_writeFile((uint8_t *)TARGETBUFFER, appLauncherSize, "nand:/launcher.dsi", &UpdateProgress) ;
  ClearProgress() ;
  
  if (!written)
  {
    Log(LOGLEVEL_ERROR, "[E] Create file failed\n    You can turn off now\n") ;
  }
  if (written < appLauncherSize)
  {
    Log(LOGLEVEL_ERROR, "[E] Write file failed\n    You can turn off now\n") ;
  }

  // BlocksDS: no explicit unmount step needed.  The fclose() inside
  // system_writeFile() already flushed the file and caused FatFs to update
  // all FAT copies.  The old nandio_shutdown() (custom-driver FAT-stage merge)
  // must NOT be called here because nandio_startup() was never invoked —
  // nandInit() uses BlocksDS's built-in NAND driver, not our custom io_dsi_nand.
  
  WaitForSuccessRestart() ;
  while(true) 
    ;

}
