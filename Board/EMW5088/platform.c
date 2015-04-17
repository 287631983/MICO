/**
******************************************************************************
* @file    platform.c 
* @author  William Xu
* @version V1.0.0
* @date    05-May-2014
* @brief   This file provides all MICO Peripherals mapping table and platform
*          specific funcgtions.
******************************************************************************
*
*  The MIT License
*  Copyright (c) 2014 MXCHIP Inc.
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy 
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights 
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is furnished
*  to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in
*  all copies or substantial portions of the Software.
*
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
*  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR 
*  IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************
*/ 

#include "stdio.h"
#include "string.h"

#include "MICOPlatform.h"
#include "platform.h"
#include "MicoDriverMapping.h"
#include "platform_common_config.h"
#include "PlatformLogging.h"
#include "sd_card.h"
#include "nvm.h"

#include "adc.h"

#if defined(MFG_MODE) || defined(BOOTLOADER)

#include "host_stor.h"
#include "fat_file.h" 
#include "host_hcd.h"
#include "dir.h"

static bool HardwareInit(DEV_ID DevId);
static FOLDER	 RootFolder;
static void FileBrowse(FS_CONTEXT* FsContext);
static bool UpgradeFileFound = false;

#endif

#define FUNC_USB_EN					   
//#define FUNC_CARD_EN					

#ifdef FUNC_USB_EN
  #define UDISK_PORT_NUM		        2		// USB�˿ڶ���
#endif

#ifdef FUNC_CARD_EN
  #define	SD_PORT_NUM                 1		// SD���˿ڶ���
#endif


/******************************************************
*                      Macros
******************************************************/

/******************************************************
*                    Constants
******************************************************/
//keep consist with miscfg.h definition
#define UPGRADE_NVM_ADDR        (176)//boot upgrade information at NVRAM address
#define UPGRADE_ERRNO_NOERR   (-1) //just initialization after boot up
#define UPGRADE_ERRNO_ENOENT  (-2) //no such file open by number
#define UPGRADE_ERRNO_EIO   (-5) //read/write error
#define UPGRADE_ERRNO_E2BIG   (-7) //too big than flash capacity
#define UPGRADE_ERRNO_EBADF   (-9) //no need to upgrade
#define UPGRADE_ERRNO_EFAULT  (-14) //address fault
#define UPGRADE_ERRNO_EBUSY   (-16) //flash lock fail
#define UPGRADE_ERRNO_ENODEV  (-19) //no upgrade device found
#define UPGRADE_ERRNO_ENODATA (-61) //flash is empty
#define UPGRADE_ERRNO_EPROTOTYPE (-91) //bad head format("MVO\x12")
#define UPGRADE_ERRNO_ELIBBAD (-80) //CRC error
#define UPGRADE_ERRNO_USBDEV  (-81) //no upgrade USB device
#define UPGRADE_ERRNO_SDDEV   (-82) //no upgrade SD device
#define UPGRADE_ERRNO_USBFILE (-83) //no upgrade file found in USB device
#define UPGRADE_ERRNO_SDFILE  (-84) //no upgrade file found in SD device
#define UPGRADE_ERRNO_NOBALL  (-85) //no upgrade ball in USB & SD device
#define UPGRADE_ERRNO_CODEMGC (-86) //wrong code magic number
#define UPGRADE_ERRNO_CODBUFDAT (-87) //code successful but data fail,because of constant data offset setting

#define UPGRADE_SUCC_MAGIC    (0x57F9B3C8) //just a successful magic
#define UPGRADE_REQT_MAGIC    (0x9ab4d18e) //just a request magic
#define UPGRADE_ERRNO_LASTCLR (0x581f9831) //just a clear magic

#define UDISK_PORT_NUM		        2		// USB PORT

/******************************************************
*                   Enumerations
******************************************************/

/******************************************************
*                 Type Definitions
******************************************************/

/******************************************************
*                    Structures
******************************************************/
typedef struct
{
    int output_pin;
    int input_pin;
} mico_gpio_test_mapping_t;

/******************************************************
*               Function Declarations
******************************************************/
extern WEAK void PlatformEasyLinkButtonClickedCallback(void);
extern WEAK void PlatformStandbyButtonClickedCallback(void);
extern WEAK void PlatformEasyLinkButtonLongPressedCallback(void);
extern WEAK void bootloader_start(void);

/******************************************************
*               Variables Definitions
******************************************************/

/* This table maps STM32 pins to GPIO definitions on the schematic
* A full pin definition is provided in <WICED-SDK>/include/platforms/BCM943362WCD4/platform.h
*/

static uint32_t _default_start_time = 0;
static mico_timer_t _button_EL_timer;

const platform_pin_mapping_t gpio_mapping[] =
{
//  /* Common GPIOs for internal use */
  [WL_GPIO1]                            = {GPIOA, 10},
  [WL_REG]                              = {GPIOB,  5},
  [MICO_SYS_LED]                        = {GPIOB, 31}, 
  [BOOT_SEL]                            = {GPIOB, 26},
  [EasyLink_BUTTON]                     = {GPIOA,  5}, 
  [STDIO_UART_RX]                       = {GPIOB,  6},
  [STDIO_UART_TX]                       = {GPIOB,  7},
  [SDIO_INT]                            = {GPIOA, 22},
  [USB_DETECT]                          = {GPIOA,  25},

//  /* GPIOs for external use */
  [APP_UART_RX]                         = {GPIOB, 29},
  [APP_UART_TX]                         = {GPIOB, 28},  
};

const mico_gpio_test_mapping_t gpio_test_mapping[] = 
{
    {0, 1},
    {2, 3},
};
/*
* Possible compile time inputs:
* - Set which ADC peripheral to use for each ADC. All on one ADC allows sequential conversion on all inputs. All on separate ADCs allows concurrent conversion.
*/
/* TODO : These need fixing */
const platform_adc_mapping_t adc_mapping[] =
{
  [MICO_ADC_1] = {1},
  [MICO_ADC_2] = {1},
  [MICO_ADC_3] = {1},
};


/* PWM mappings */
const platform_pwm_mapping_t pwm_mappings[] =
{
  [MICO_PWM_1]  = {1},    /* or TIM10/Ch1                       */
  [MICO_PWM_2]  = {1},    /* or TIM1/Ch2N                       */
  [MICO_PWM_3]  = {1},    
  /* TODO: fill in the other options here ... */
};

const platform_spi_mapping_t spi_mapping[] =
{
  [MICO_SPI_1]  =
  {
    1
  }
};

const platform_uart_mapping_t uart_mapping[] =
{
[MICO_UART_1] =
  {
    .uart                            = FUART,
    .pin_tx                          = &gpio_mapping[STDIO_UART_TX],
    .pin_rx                          = &gpio_mapping[STDIO_UART_RX],
    .pin_cts                         = NULL,
    .pin_rts                         = NULL,
  },
  [MICO_UART_2] =
  {
    .uart                            = BUART,
    .pin_tx                          = &gpio_mapping[APP_UART_TX],
    .pin_rx                          = &gpio_mapping[APP_UART_RX],
    .pin_cts                         = NULL,
    .pin_rts                         = NULL,
  },
};

const platform_i2c_mapping_t i2c_mapping[] =
{
  [MICO_I2C_1] =
  {
    1,
  },
};

/******************************************************
*               Function Definitions
******************************************************/

static void _button_EL_irq_handler( void* arg )
{
  (void)(arg);
  int interval = -1;

  mico_start_timer(&_button_EL_timer);
  
  if ( MicoGpioInputGet( (mico_gpio_t)EasyLink_BUTTON ) == 0 ) {
    _default_start_time = mico_get_time()+1;
    mico_start_timer(&_button_EL_timer);
    MicoGpioEnableIRQ( (mico_gpio_t)EasyLink_BUTTON, IRQ_TRIGGER_RISING_EDGE, _button_EL_irq_handler, NULL );
  } else {
    interval = mico_get_time() + 1 - _default_start_time;
    if ( (_default_start_time != 0) && interval > 50 && interval < RestoreDefault_TimeOut){
      /* EasyLink button clicked once */
      PlatformEasyLinkButtonClickedCallback();
      //platform_log("PlatformEasyLinkButtonClickedCallback!");
      MicoGpioOutputLow( (mico_gpio_t)MICO_RF_LED );
      MicoGpioEnableIRQ( (mico_gpio_t)EasyLink_BUTTON, IRQ_TRIGGER_FALLING_EDGE, _button_EL_irq_handler, NULL );
   }
   mico_stop_timer(&_button_EL_timer);
   _default_start_time = 0;
  }
}

//static void _button_STANDBY_irq_handler( void* arg )
//{
//  (void)(arg);
//  PlatformStandbyButtonClickedCallback();
//}

static void _button_EL_Timeout_handler( void* arg )
{
  (void)(arg);
  _default_start_time = 0;
  MicoGpioEnableIRQ( (mico_gpio_t)EasyLink_BUTTON, IRQ_TRIGGER_FALLING_EDGE, _button_EL_irq_handler, NULL );
  if( MicoGpioInputGet( (mico_gpio_t)EasyLink_BUTTON ) == 0){
    //platform_log("PlatformEasyLinkButtonLongPressedCallback!");
    PlatformEasyLinkButtonLongPressedCallback();
  }
  mico_stop_timer(&_button_EL_timer);
}

bool watchdog_check_last_reset( void )
{
//  if ( RCC->CSR & RCC_CSR_WDGRSTF )
//  {
//    /* Clear the flag and return */
//    RCC->CSR |= RCC_CSR_RMVF;
//    return true;
//  }
//  
  return false;
}

OSStatus mico_platform_init( void )
{
#ifdef DEBUG
  #if defined(__CC_ARM)
    platform_log("Build by Keil");
  #elif defined (__IAR_SYSTEMS_ICC__)
    platform_log("Build by IAR");
  #endif
#endif
 platform_log( "Mico platform initialised" );
 if ( true == watchdog_check_last_reset() )
 {
   platform_log( "WARNING: Watchdog reset occured previously. Please see watchdog.c for debugging instructions." );
 }
  
  return kNoErr;
}

void init_platform( void )
{
  MicoGpioInitialize( (mico_gpio_t)MICO_SYS_LED, OUTPUT_PUSH_PULL );
  MicoSysLed(false);

  /* USB-HOST */
  MicoGpioInitialize( (mico_gpio_t)USB_DETECT, INPUT_PULL_DOWN );
  //  Initialise EasyLink buttons
  //MicoGpioInitialize( (mico_gpio_t)EasyLink_BUTTON, INPUT_PULL_UP );
  //mico_init_timer(&_button_EL_timer, RestoreDefault_TimeOut, _button_EL_Timeout_handler, NULL);
  //MicoGpioEnableIRQ( (mico_gpio_t)EasyLink_BUTTON, IRQ_TRIGGER_FALLING_EDGE, _button_EL_irq_handler, NULL );
//  
//  //  Initialise Standby/wakeup switcher
//  MicoGpioInitialize( Standby_SEL, INPUT_PULL_UP );
//  MicoGpioEnableIRQ( Standby_SEL , IRQ_TRIGGER_FALLING_EDGE, _button_STANDBY_irq_handler, NULL);

}

#ifdef BOOTLOADER


void init_platform_bootloader( void )
{
  uint32_t BootNvmInfo;
  OSStatus err;
  
  MicoGpioInitialize( BOOT_SEL, INPUT_PULL_UP );
  return;
  
  /* Check USB-HOST is inserted */
  err = MicoGpioInitialize( USB_DETECT, INPUT_PULL_DOWN );
  require_noerr(err, exit);
  mico_thread_msleep_no_os(2);
  
  require_string( MicoGpioInputGet( USB_DETECT ) == true, exit, "USB device is not inserted" );

  if( HardwareInit(DEV_ID_USB) ){
    FolderOpenByNum(&RootFolder, NULL, 1);
    FileBrowse(RootFolder.FsContext);
  }

  /* Check last firmware update is success or not. */
  NvmRead(UPGRADE_NVM_ADDR, (uint8_t*)&BootNvmInfo, 4);

  if(false == UpgradeFileFound)
  {
    if(BootNvmInfo == UPGRADE_SUCC_MAGIC)
    {
      /*
       * boot up check for the last time
       */
      platform_log("[UPGRADE]:upgrade successful completely");
    }
    else if(BootNvmInfo == (uint32_t)UPGRADE_ERRNO_NOERR)
    {
      platform_log("[UPGRADE]:no upgrade, boot normallly");
    }
    else if(BootNvmInfo == (uint32_t)UPGRADE_ERRNO_CODBUFDAT)
    {
      platform_log("[UPGRADE]:upgrade successful partly, data fail");
    }
    else
    {
      platform_log("[UPGRADE]:upgrade error, errno = %d", (int32_t)BootNvmInfo);
    }
  }
  else
  {
    if(BootNvmInfo == (uint32_t)UPGRADE_ERRNO_NOERR)
    {
      platform_log("[UPGRADE]:found upgrade ball, prepare to boot upgrade");
      BootNvmInfo = UPGRADE_REQT_MAGIC;
      NvmWrite(UPGRADE_NVM_ADDR, (uint8_t*)&BootNvmInfo, 4);
            //if you want PORRESET to reset GPIO only,uncomment it
            //GpioPorSysReset(GPIO_RSTSRC_PORREST);
      NVIC_SystemReset();
      while(1);;;
    }
    else if(BootNvmInfo == UPGRADE_SUCC_MAGIC)
    {
      BootNvmInfo = (uint32_t)UPGRADE_ERRNO_NOERR;
      NvmWrite(UPGRADE_NVM_ADDR, (uint8_t*)&BootNvmInfo, 4);
      platform_log("[UPGRADE]:found upgrade ball file for the last time, re-plugin/out, if you want to upgrade again");
    }
    else
    {
      platform_log("[UPGRADE]:upgrade error, errno = %d", (int32_t)BootNvmInfo);
      if( BootNvmInfo == (uint32_t)UPGRADE_ERRNO_EBADF ) {
        platform_log("[UPGRADE]:Same file, no need to update");
        goto exit;
      }
      BootNvmInfo = (uint32_t)UPGRADE_ERRNO_NOERR;
      NvmWrite(UPGRADE_NVM_ADDR, (uint8_t*)&BootNvmInfo, 4);
      BootNvmInfo = UPGRADE_REQT_MAGIC;
      NvmWrite(UPGRADE_NVM_ADDR, (uint8_t*)&BootNvmInfo, 4);
            //if you want PORRESET to reset GPIO only,uncomment it
            //GpioPorSysReset(GPIO_RSTSRC_PORREST);
      NVIC_SystemReset();
    }
  }
exit:
  return;
}
#endif

#if defined(MFG_MODE) || defined(BOOTLOADER)

static bool IsCardLink(void)
{
  return false;
}
static bool IsUDiskLink(void)
{
	return UsbHost2IsLink();
}

bool CheckAllDiskLinkFlag(void)
{
    return TRUE;
}

static bool HardwareInit(DEV_ID DevId)
{
	switch(DevId)
	{
		case DEV_ID_SD:
			if(!IsCardLink())
			{
				return FALSE;
			}
			FSDeInit(DEV_ID_SD);
			if(SdCardInit())	
			{
				return FALSE;
			}
			if(!FSInit(DEV_ID_SD))
			{
				return FALSE;
			}
			return TRUE;
		case DEV_ID_USB:
			Usb2SetDetectMode(1, 0);
			UsbSetCurrentPort(UDISK_PORT_NUM);
			if(!IsUDiskLink())
			{
				return FALSE;
			}
			FSDeInit(DEV_ID_SD);
			FSDeInit(DEV_ID_USB);
			if(!HostStorInit())
			{
				return FALSE;
			}
			if(!FSInit(DEV_ID_USB))
			{
				return FALSE;
			}
			return TRUE;
		default:
			break;
	}
	return FALSE;
}
static void FileBrowse(FS_CONTEXT* FsContext)
{	
	uint8_t	EntryType;
	DirSetStartEntry(FsContext, FsContext->gFsInfo.RootStart, 0, TRUE);
	FsContext->gFolderDirStart = FsContext->gFsInfo.RootStart;
	while(1)
	{
		EntryType = DirGetNextEntry(FsContext);
		switch(EntryType)
		{
			case ENTRY_FILE:
#if 0
				platform_log("%-.11s  %d��%d��%d�� %d:%d:%d  %d �ֽ�",
					FsContext->gCurrentEntry->FileName,
					1980+(FsContext->gCurrentEntry->CreateDate >> 9),
					(FsContext->gCurrentEntry->CreateDate >> 5) & 0xF,
					(FsContext->gCurrentEntry->CreateDate) & 0x1F,
					FsContext->gCurrentEntry->CreateTime >> 11,
					(FsContext->gCurrentEntry->CreateTime >> 5) & 0x3F,
					((FsContext->gCurrentEntry->CreateTime << 1) & 0x3F) + (FsContext->gCurrentEntry->CrtTimeTenth / 100),
					FsContext->gCurrentEntry->Size);
#endif
      	if((FsContext->gCurrentEntry->ExtName[0] == 'M') && 
           (FsContext->gCurrentEntry->ExtName[1] == 'V') && 
           (FsContext->gCurrentEntry->ExtName[2] == 'A')){
            UpgradeFileFound = true;
            return;
          }
				break;
			case ENTRY_FOLDER:
				break;
			case ENTRY_END:
        return;
			default:
				break;
		}
	}
}

#endif

void host_platform_power_wifi( bool power_enabled )
{
  if ( power_enabled == true )
  {
    MicoGpioOutputLow( (mico_gpio_t)WL_REG );  
  }
  else
  {
    MicoGpioOutputHigh( (mico_gpio_t)WL_REG ); 
  }
}

void MicoSysLed(bool onoff)
{
    if (onoff) {
        MicoGpioOutputLow( (mico_gpio_t)MICO_SYS_LED );
    } else {
        MicoGpioOutputHigh( (mico_gpio_t)MICO_SYS_LED );
    }
}

void MicoRfLed(bool onoff)
{
    if (onoff) {
        MicoGpioOutputLow( (mico_gpio_t)MICO_RF_LED );
    } else {
        MicoGpioOutputHigh( (mico_gpio_t)MICO_RF_LED );
    }
}

bool MicoShouldEnterMFGMode(void)
{
//  if(MicoGpioInputGet((mico_gpio_t)BOOT_SEL)==false && MicoGpioInputGet((mico_gpio_t)MFG_SEL)==false)
//    return true;
//  else
    return false;
}

bool MicoShouldEnterBootloader(void)
{
  if(MicoGpioInputGet(BOOT_SEL)==false)
    return true;
  else
    return false;
}
#ifdef MFG_MODE
static int gpio_result=0, gpio_num;
static char test_result[32];

static int gpio_test_one( int index)
{
    mico_gpio_t in, out;

    in  = gpio_test_mapping[index].input_pin;
    out = gpio_test_mapping[index].output_pin;

    MicoGpioInitialize(in, INPUT_PULL_UP);
    MicoGpioInitialize(out, OUTPUT_PUSH_PULL);

    MicoGpioOutputHigh(out);
    msleep(1);
    if (MicoGpioInputGet(in) != true)
        return 0;
    
    MicoGpioOutputLow(out);
    msleep(1);
    if (MicoGpioInputGet(in) != false)
        return 0;
    
    return 1;
}

static int gpio_test(void)
{
    int i;
    int ret = 1;
    
    gpio_result = 0;
    for(i=0; i<gpio_num; i++) {
        if (gpio_test_one(i) == 1) {
            gpio_result |= (1<<i);
        } else {
            ret = 0;
        }
    }

    return ret;
}

int get_gpio_num(void)
{
    return gpio_num;
}

int get_gpio_test_result(void)
{
    return gpio_result;
}

char *mxchip_gpio_test(void)
{
    int ret;
    
    gpio_num = sizeof(gpio_test_mapping) / sizeof(mico_gpio_test_mapping_t);
    ret = gpio_test();
    if (ret == 1)
        return "PASS";
    
    sprintf(test_result, "Fail: %d:%x", gpio_num, gpio_result);
    return test_result;    
}

/* Adc for PB25 ==> Status PIN */
char *mxchip_pwr_test(void)
{
    uint16_t value;
    
    SarAdcGpioSel(ADC_CHANNEL_B25);
    value = SarAdcChannelGetValue(ADC_CHANNEL_B25);
    sprintf(test_result, "%u", value);
    return test_result;
}

/* Only for 5088 & 3088, check USB connected?
  * no: return USB check result
  * yes: reboot, use mx1101's bootloader do USB upgrade.
  *        after upgrade, the mxchip bootloader will check upgrade result and report to test tool.
  */
char *mxchip_upgrade_test(void)
{
    uint32_t BootNvmInfo;
    
    if( MicoGpioInputGet( (mico_gpio_t)USB_DETECT ) == false) {
        return "No USB device";
    } else {
        mxchiP_need_upgrade();
        return "Upgrading";
    }
}

int mxchip_upgrade(void)
{
    uint32_t BootNvmInfo;
    
    if( HardwareInit(DEV_ID_USB) ){
        FolderOpenByNum(&RootFolder, NULL, 1);
        FileBrowse(RootFolder.FsContext);
    }
    
    if(false == UpgradeFileFound) {
        MicoUartSend(MICO_UART_1, "File not found", strlen("File not found"));
    } else {
      BootNvmInfo = UPGRADE_REQT_MAGIC;
      NvmWrite(UPGRADE_NVM_ADDR, (uint8_t*)&BootNvmInfo, 4);
    }
    
    NVIC_SystemReset();
    while(1);
}
#endif

