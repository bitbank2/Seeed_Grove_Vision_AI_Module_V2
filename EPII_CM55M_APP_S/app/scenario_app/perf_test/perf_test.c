#include "perf_test.h"
#include "hx_drv_iic.h"
#include "hx_drv_gpio.h"
#include "hx_drv_scu_export.h"
#include "hx_drv_spi.h"
#include "../../../library/spi_ptl/spi_master_protocol.h"

#define FREERTOS

#ifdef FREERTOS
/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#endif

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240

#include "Arduino.h"
//#include "oled.inl"
#include "spi_lcd.inl"
#include "Roboto_Black_40.h"
#define __LINUX__
#include "gif.inl"
#include "badger_240x240.h"
//#include "badgers.h"
GIFIMAGE gif;
#include <arm_mve.h>

#ifdef TRUSTZONE_SEC
#if (__ARM_FEATURE_CMSE & 1) == 0
#error "Need ARMv8-M security extensions"
#elif (__ARM_FEATURE_CMSE & 2) == 0
#error "Compile with --cmse"
#endif
#include "arm_cmse.h"
#ifdef NSC
#include "veneer_table.h"
#endif
/* Trustzone config. */

#ifndef TRUSTZONE_SEC_ONLY
/* FreeRTOS includes. */
#include "secure_port_macros.h"
#endif
#endif

/* Task priorities. */
#define hello_task1_PRIORITY	(configMAX_PRIORITIES - 1)
#define hello_task2_PRIORITY	(configMAX_PRIORITIES - 1)

#include "xprintf.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
static void hello_task1(void *pvParameters);
static void hello_task2(void *pvParameters);

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
#define CPU_CLK 0xffffff+1

/*******************************************************************************
 * Code
 ******************************************************************************/
//
// Use gather read to convert a set of 8-bit pixels through a palette
// into 16-bit pixels. The SIMD loop processes 16 pixels at a time
// and falls through to reference C code for any odd pixels remaining
//
void PaletteExpand_Helium(uint8_t *pSrc, uint16_t *pDest, uint16_t *pPalette, int iCount)
{
int i;
	// process 16 at a time
	for (i=0; i<iCount-15; i+=16) {
	   uint16x8_t out0, out1, off16_0, off16_1;

	   off16_0 = vldrbq_u16(pSrc); // read 8-bit pixels and widen to 16
           pSrc += 8;
	   off16_1 = vldrbq_u16(pSrc);
	   pSrc += 8;
           out0 = vldrhq_gather_shifted_offset_u16(pPalette, off16_0);
           out1 = vldrhq_gather_shifted_offset_u16(pPalette, off16_1);
           vst1q_u16(pDest, out0); // store 8 translated pixels
           pDest += 8;
	   vst1q_u16(pDest, out1); // store another 8 pixels
	   pDest += 8;
	} // for each set of 16 pixels

	// leftover pixels
	for (; i<iCount; i++) {
            *pDest++ = pPalette[*pSrc++];
	}
} /* PaletteExpand_Helium() */

void GIFDraw(GIFDRAW *pDraw)
{
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[320];
    int x, y, iWidth;

    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // current line
    iWidth = pDraw->iWidth;
    if (iWidth > DISPLAY_WIDTH)
       iWidth = DISPLAY_WIDTH;
    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) // restore to background color
    {
      for (x=0; x<iWidth; x++)
      {
        if (s[x] == pDraw->ucTransparent)
           s[x] = pDraw->ucBackground;
      }
      pDraw->ucHasTransparency = 0;
    }
    // Apply the new pixels to the main image
    if (pDraw->ucHasTransparency) // if transparency used
    {
      uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
      int x, iCount;
      pEnd = s + iWidth;
      x = 0;
      iCount = 0; // count non-transparent pixels
      while(x < iWidth)
      {
        c = ucTransparent-1;
        d = usTemp;
        while (c != ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent) // done, stop
          {
            s--; // back up to treat it like transparent
          }
          else // opaque
          {
             *d++ = usPalette[c];
             iCount++;
          }
        } // while looking for opaque pixels
        if (iCount) // any opaque pixels?
        {
          lcdSetPosition(20+pDraw->iX+x, y, iCount, 1);
          lcdWriteDATA((uint8_t *)usTemp, iCount*2);
          x += iCount;
          iCount = 0;
        }
        // no, look for a run of transparent pixels
        c = ucTransparent;
        while (c == ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent)
             iCount++;
          else
             s--;
        }
        if (iCount)
        {
          x += iCount; // skip these
          iCount = 0;
        }
      }
    }
    else
    {
      s = pDraw->pPixels;
      // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
      PaletteExpand_Helium(s, usTemp, usPalette, iWidth);
//      for (x=0; x<iWidth; x++)
//        usTemp[x] = usPalette[*s++];
      lcdSetPosition(20+pDraw->iX, y, iWidth, 1);
      lcdWriteDATA((uint8_t *)usTemp, iWidth*2);
    }
} /* GIFDraw() */

/*!
 * @brief Main function
 */
int app_main(void)
{
uint8_t u8RST = 0xd2;
uint8_t u8DC = 0xd7;
uint8_t u8CS = 0xd6;
uint8_t u8BL = 0xff;
uint32_t systick1, systick2;
uint32_t loop_cnt1, loop_cnt2;
char szTemp[32];
int rc, iFrame;

    printf("Starting perf test app...\n\r");
    lcdInit(LCD_ST7789_240x280, 48000000, u8CS, u8DC, u8RST, u8BL, 0, 0);
    lcdFill(0);
    lcdWriteStringCustom(&Roboto_Black_40, 32, 40, "Cortex-M55", COLOR_BLUE, COLOR_BLACK, 0);
    lcdWriteStringCustom(&Roboto_Black_40, 42, 80, "Test Rig", COLOR_GREEN, COLOR_BLACK, 0);

    EPII_Set_Systick_enable(1); // 64MHz tick clock
    GIF_begin(&gif, BIG_ENDIAN_PIXELS);
    printf("Starting GIF decoder...\n\r");
  //  rc = GIF_openRAM(&gif, (uint8_t *)ucBadgers, sizeof(ucBadgers), GIFDraw);
    rc = GIF_openRAM(&gif, (uint8_t *)badger_240x240, sizeof(badger_240x240), GIFDraw);
    if (rc)
    {
        printf("Successfully opened GIF\n\r");
        printf("Image size: %d x %d\n\r", GIF_getCanvasWidth(&gif), GIF_getCanvasHeight(&gif));
        iFrame = 0;
	EPII_Set_Systick_load(1000000000);
	EPII_Get_Systick_val(&systick1);
        while (GIF_playFrame(&gif, NULL, NULL))
        {
            iFrame++;
        }
	delay(100);
	EPII_Get_Systick_val(&systick2);
	rc = (systick1-systick2); // tick timer counts down
	rc >>= 6; // divide by 64 to get microseconds
	printf("Successfully decoded %d frames in %d us\n\r", iFrame, rc);
        if (GIF_getComment(&gif, szTemp))
            printf("GIF Comment: \"%s\"\n\r", szTemp);
        GIF_close(&gif);
    }

//    EPII_Set_Systick_enable(1);
//    oledInit(0x3c, 400000);
//    oledFill(0);
    //SystemGetTick(&systick_1, &loop_cnt_1);
//    oledWriteString(0,0,"Cortex-M55", FONT_12x16,0);
//    oledWriteString(0,16,"SIMD Perf Test", FONT_8x8, 0);
  //  SystemGetTick(&systick_2, &loop_cnt_2);
  //  sprintf(szTemp, "CPU_CLK %d", CPU_CLK);
  //  oledWriteString(0,24,szTemp, FONT_8x8, 0);
  //  sprintf(szTemp, "tick %d",(loop_cnt_2-loop_cnt_1)*CPU_CLK+(systick_1-systick_2));
  //  oledWriteString(0,32,szTemp, FONT_8x8, 0);
//    while (1) {
    //    SystemGetTick(&systick_1, &loop_cnt_1);
//        systick_1 += (loop_cnt_1 * CPU_CLK);
//        EPII_Get_Systick_val(&systick_1);
//        sprintf(szTemp, "%d", systick_1/0x100000);
//        oledWriteString(0,48,szTemp, FONT_8x8, 0);
//    }
//    if ( xTaskCreate(hello_task1, "Hello_task1", 512, NULL, hello_task1_PRIORITY, NULL) != pdPASS )
//    {
//        printf("Hello_task1 creation failed!.\r\n");
//        while (1)
//        	;
//    }

//    if ( xTaskCreate(hello_task2, "Hello_task2", 512, NULL, hello_task2_PRIORITY, NULL) != pdPASS )
//    {
//        printf("Hello_task2 creation failed!.\r\n");
//        while (1)
//            ;
//    }

//    vTaskStartScheduler();

    for (;;);
}

/*!
 * @brief Task responsible for printing of "Hello world." message.
 */
static void hello_task1(void *pvParameters)
{
    for (;;)
    {
    	printf("Hello world freertos task1.\r\n");
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

static void hello_task2(void *pvParameters)
{
    for (;;)
    {
    	printf("Hello world freertos task2.\r\n");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

