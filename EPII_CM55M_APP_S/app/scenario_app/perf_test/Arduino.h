#ifndef _ARDUINO_H_
#define _ARDUINO_H_
#define PROGMEM

//
// wrapper functions for Arduino-like APIs
// written by Larry Bank
// project started 2/8/2024
//
volatile uint8_t bBusy, bDMA;

// GPIO modes
enum {
    INPUT = 0,
    INPUT_PULLUP,
    OUTPUT
};

void tx_callback(void *param)
{
   bBusy = 0;
}

void dma_callback(void *status)
{
   bDMA = 0;
   digitalWrite(0xd6/*u8CS*/, 1); // DEBUG
}

void SPI_write(uint8_t *pData, int iLen)
{
DEV_SPI_PTR dev_spi_m;
int32_t busy_status = E_NORES;
uint32_t u32;

   if (iLen <= 0) return;

   while (bDMA) {};

   dev_spi_m = hx_drv_spi_mst_get_dev(USE_DW_SPI_MST_S);
//   dev_spi_m->set_control(SPI_CMD_SET_TXINT_BUF, (SPI_CTRL_PARAM)pData);
//    u32 = 2; // enable TX only
//    dev_spi_m->spi_control(SPI_CMD_ENA_DMA_TXRX, (SPI_CTRL_PARAM)u32);
//    u32 = 1; // enable interrupt on TX
//    dev_spi_m->spi_control(SPI_CMD_SET_TXINT, (SPI_CTRL_PARAM)u32);
//   dev_spi_m->spi_control(SPI_CMD_SET_DMA_TXCB, (SPI_CTRL_PARAM)tx_callback);
//   dev_spi_m->spi_control(SPI_CMD_SET_TXCB, (SPI_CTRL_PARAM)tx_callback);
      bDMA = 1;
      while (busy_status != E_OK) {
         busy_status = dev_spi_m->spi_write_dma(pData, iLen, dma_callback);
      }
   //   while (dev_spi_m->spi_info.dma_txdl > 0) {
//       dev_spi_m->spi_control(SPI_CMD_GET_TX_DMA_DONE, (SPI_CTRL_PARAM)&busy_status);
//    }
//     dev_spi_m->spi_write_dma(pData, iLen, dma_callback);
//   dev_spi_m->spi_write(pData, iLen);
//   if (iLen < 20) { // wait for completion of short sequences
      while (bDMA) {  };
//   }
} /* SPI_write() */

void SPI_begin(uint32_t u32Speed, int iMode, uint8_t u8MOSI, uint8_t u8CLK)
{
DEV_SPI_PTR dev_spi_m;
uint32_t max_clk, u32;

    (void)u8MOSI; (void)u8CLK; // not used
#ifndef OLD_WAY
    hx_drv_scu_set_PB2_pinmux(SCU_PB2_PINMUX_SPI_M_DO_1, 1); // MOSI
    hx_drv_scu_set_PB3_pinmux(SCU_PB3_PINMUX_SPI_M_DI_1, 1); // not used
    hx_drv_scu_set_PB4_pinmux(SCU_PB4_PINMUX_SPI_M_SCLK_1, 1);
  //  hx_drv_scu_set_PB6_pinmux(SCU_PB6_PINMUX_SPI_M_CS_1, 1); // DEBUG
    hx_drv_spi_mst_open_speed(u32Speed);
//    dev_spi_m = hx_drv_spi_mst_get_dev(USE_DW_SPI_MST_S);
//    u32 = 2; // enable TX only
//    dev_spi_m->spi_control(SPI_CMD_ENA_DMA_TXRX, (SPI_CTRL_PARAM)u32);
//    u32 = 1; // enable interrupt on TX
//    dev_spi_m->spi_control(SPI_CMD_SET_TXINT, (SPI_CTRL_PARAM)u32);

    ///for example, with RC96M enabled, set SSPI output clock = 48HMZ. 
    ///ref. clock source is selected to RC96M and divider is 0
//    hx_drv_spi_mst_init(USE_DW_SPI_MST_S, DW_SPI_S_RELBASE); // init SPI master
//    hx_drv_scu_set_pdlsc_sspimclk_cfg(SCU_LSCSSPIMSRC_RC96M48M, 0);
//    dev_spi_m = hx_drv_spi_mst_get_dev(USE_DW_SPI_MST_S);
    ///update information of reference clock, and get max output clock(max_clk = 48000000)
//    dev_spi_m->spi_control(SPI_CMD_MST_UPDATE_SYSCLK, (SPI_CTRL_PARAM)&max_clk);
 //   dev_spi_m->spi_control(SPI_CMD_SET_CLK_MODE, (SPI_CTRL_PARAM)iMode);
    if (u32Speed >= 24000000 && u32Speed < 48000000) // div2
	max_clk /= 2;
    else if (u32Speed >= 12000000 && u32Speed < 24000000) // div4
	max_clk /= 4;
    else if (u32Speed >= 6000000 && u32Speed < 12000000) // div8
	max_clk /= 8;
    else if (u32Speed >= 3000000 && u32Speed < 6000000) // div16
	max_clk /= 16;
    else
	max_clk /= 32; // div32
 //   dev_spi_m->spi_open(DEV_MASTER_MODE, max_clk);
#else
        SCU_PINMUX_CFG_T pinmux_cfg;

        hx_drv_scu_get_all_pinmux_cfg(&pinmux_cfg);

        /* Init SPI master pin mux (share with SDIO) */
        pinmux_cfg->pin_pb2 = SCU_PB2_PINMUX_SPI_M_DO_1;
        pinmux_cfg->pin_pb4 = SCU_PB4_PINMUX_SPI_M_CLK_1;
       // pinmux_cfg->pin_pb6 = SCU_PB6_PINMUX_SPI_M_CS; // DEBUG
        hx_drv_scu_set_all_pinmux_cfg(&pinmux_cfg, 1);
#endif
    bDMA = 0;

} /* SPI_begin() */

void I2CInit(int iSpeed)
{
    int i;
    if (iSpeed <= 100000) i = DW_IIC_SPEED_STANDARD; // 100K
    else if (iSpeed > 100000 && iSpeed <= 400000) i = DW_IIC_SPEED_FAST;
    else i = DW_IIC_SPEED_FASTPLUS; // 1000000
    // initialize I2C master on port 0
    hx_drv_scu_set_PA2_pinmux(SCU_PA2_PINMUX_I2C_M_SCL, 1);
    hx_drv_scu_set_PA3_pinmux(SCU_PA3_PINMUX_I2C_M_SDA, 1);
    hx_drv_i2cm_init(USE_DW_IIC_0, HX_I2C_HOST_MST_0_BASE, i);
    bBusy = 0;
} /* I2CInit() */

void I2CWrite(uint8_t addr, uint8_t *pBuf, int iLen)
{
	while (bBusy) {};
	hx_drv_i2cm_interrupt_write(USE_DW_IIC_0, addr, pBuf, iLen, tx_callback);
        bBusy = 1;
        while (bBusy) {}; // since we are using a single buffer, wait for completion
} /* I2CWrite() */

void pinMode(uint8_t u8Pin, int iMode)
{
    if (iMode == OUTPUT) {
    switch (u8Pin) { // only a few exposed 3.3v pins that we use
        case 0xd2: // PB8
            hx_drv_gpio_set_output(GPIO2, GPIO_OUT_HIGH);
            hx_drv_scu_set_PB8_pinmux(SCU_PB8_PINMUX_GPIO2_2, 1);
            hx_drv_gpio_init(GPIO_GROUP_0, HX_GPIO_GROUP_0_BASE);
            break;
        case 0xd6:
            hx_drv_gpio_set_output(GPIO0, GPIO_OUT_HIGH);
            hx_drv_scu_set_PB6_pinmux(SCU_PB6_PINMUX_GPIO0_2, 1);
            hx_drv_gpio_init(GPIO_GROUP_0, HX_GPIO_GROUP_0_BASE);
            break;
        case 0xd7:
            hx_drv_gpio_set_output(GPIO1, GPIO_OUT_HIGH);
            hx_drv_scu_set_PB7_pinmux(SCU_PB7_PINMUX_GPIO1_2, 1);
            hx_drv_gpio_init(GPIO_GROUP_0, HX_GPIO_GROUP_0_BASE);
            break;
    } // switch
    } else { // input mode
    }
} /* pinMode() */

void digitalWrite(uint8_t u8Pin, uint8_t u8Val)
{
   switch (u8Pin) {
    case 0xd2: // PB8
        hx_drv_gpio_set_out_value(GPIO2, u8Val);
        break;
    case 0xd6: // PB6
        hx_drv_gpio_set_out_value(GPIO0, u8Val);
        break;
    case 0xd7: // PB7
        hx_drv_gpio_set_out_value(GPIO1, u8Val);
        break;
   }
}
uint8_t digitalRead(uint8_t u8Pin)
{
    return 0;
}
void delay(uint32_t u32Millis)
{
    hx_drv_timer_cm55x_delay_ms(u32Millis, TIMER_STATE_DC);
}
#endif // _ARDUINO_H_
