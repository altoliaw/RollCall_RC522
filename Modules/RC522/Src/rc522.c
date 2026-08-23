/**
 * @file    rc522.c
 * @brief   Implementation of the MFRC522 RFID reader driver module.
 *
 * This translation unit implements the reference MFRC522 initialization,
 * anti-collision, and select sequence documented in the NXP MFRC522
 * datasheet and widely used in reference driver ports; the accepted design
 * reference for this project is a.md, Section "Firmware Receiver-Side
 * (RC522 Card-Read Flow) Architecture Diagram" and its accompanying
 * function reference table.
 * @note    This implementation has not yet been validated against physical
 *          hardware or a physical card; a.md, Sections 2 and 3, record this
 *          as an open verification item.
 */

#include "../Inc/rc522.h"

/* -------------------------------------------------------------------------
 * Private register map, command codes, and bit masks
 *
 * Addresses and masks below follow the MFRC522 datasheet, Section 9.3
 * (Register Overview); they are implementation details of this module and
 * are intentionally not exposed through rc522.h.
 * ---------------------------------------------------------------------- */

#define RC522_REG_COMMAND      ((uint8_t)0x01U) /**< Starts and stops command execution. */
#define RC522_REG_COMM_IEN     ((uint8_t)0x02U) /**< Enables and disables interrupt request control bits. */
#define RC522_REG_COMM_IRQ     ((uint8_t)0x04U) /**< Reports interrupt request bits. */
#define RC522_REG_DIV_IRQ      ((uint8_t)0x05U) /**< Reports the CRC-coprocessor interrupt request bit. */
#define RC522_REG_ERROR        ((uint8_t)0x06U) /**< Reports the error status of the last command executed. */
#define RC522_REG_FIFO_DATA    ((uint8_t)0x09U) /**< Provides read and write access to the 64-byte FIFO buffer. */
#define RC522_REG_FIFO_LEVEL   ((uint8_t)0x0AU) /**< Indicates the number of bytes stored in the FIFO buffer. */
#define RC522_REG_CONTROL      ((uint8_t)0x0CU) /**< Reports miscellaneous control bits, including RxLastBits. */
#define RC522_REG_BIT_FRAMING  ((uint8_t)0x0DU) /**< Adjusts bit-oriented frame settings for transmission. */
#define RC522_REG_MODE         ((uint8_t)0x11U) /**< Defines general transmit and receive mode settings. */
#define RC522_REG_TX_CONTROL   ((uint8_t)0x14U) /**< Controls the logical behavior of the antenna driver pins. */
#define RC522_REG_TX_ASK       ((uint8_t)0x15U) /**< Controls the setting of the transmission modulation. */
#define RC522_REG_CRC_RESULT_M ((uint8_t)0x21U) /**< Holds the MSB of the CRC coprocessor's result. */
#define RC522_REG_CRC_RESULT_L ((uint8_t)0x22U) /**< Holds the LSB of the CRC coprocessor's result. */
#define RC522_REG_TMODE        ((uint8_t)0x2AU) /**< Defines the internal timer's settings. */
#define RC522_REG_TPRESCALER   ((uint8_t)0x2BU) /**< Defines the lower 8 bits of the timer prescaler. */
#define RC522_REG_TRELOAD_H    ((uint8_t)0x2CU) /**< Defines the higher 8 bits of the timer reload value. */
#define RC522_REG_TRELOAD_L    ((uint8_t)0x2DU) /**< Defines the lower 8 bits of the timer reload value. */
#define RC522_REG_VERSION      ((uint8_t)0x37U) /**< Reports the chip's version and subversion number. */

#define RC522_PCD_IDLE          ((uint8_t)0x00U) /**< Cancels the current command and returns to idle. */

#define RC522_TXCONTROL_RF_EN   ((uint8_t)0x03U) /**< Enables both antenna driver outputs (Tx1RFEn and Tx2RFEn) when set. */
#define RC522_ERROR_MASK        ((uint8_t)0x1BU) /**< Combines the ProtocolErr, ParityErr, CollErr, and BufferOvfl bits into a single error mask. */
#define RC522_FIFO_FLUSH        ((uint8_t)0x80U) /**< Clears the FIFO buffer when written to FIFOLevelReg's FlushBuffer bit. */
#define RC522_BITFRAMING_START  ((uint8_t)0x80U) /**< Starts transmission when set in BitFramingReg's StartSend bit. */
#define RC522_COMIRQ_TIMER      ((uint8_t)0x01U) /**< Indicates that the reader's internal timer has expired. */
#define RC522_COMIRQ_RX_IDLE    ((uint8_t)0x30U) /**< Combines the RxIRq and IdleIRq bits, signaling command completion. */
#define RC522_DIVIRQ_CRC        ((uint8_t)0x04U) /**< Indicates that the CRC coprocessor has finished computing its result. */

#define RC522_CASCADE_TAG       ((uint8_t)0x88U) /**< Marks a UID that spans a second cascade level. */
#define RC522_PICC_ANTICOLL_CL2 ((uint8_t)0x95U) /**< Issues the anti-collision or select command for cascade level 2. */

/** Formats a register address into the write byte expected by the MFRC522
 *  SPI protocol: MSB cleared, address in bits 6:1, LSB cleared. */
#define RC522_SPI_ADDR_WRITE(addr)  ((uint8_t)(((uint8_t)(addr) << 1) & 0x7EU))

/** Formats a register address into the read byte expected by the MFRC522
 *  SPI protocol: MSB set, address in bits 6:1, LSB cleared. */
#define RC522_SPI_ADDR_READ(addr)   ((uint8_t)(RC522_SPI_ADDR_WRITE(addr) | 0x80U))

/** Bounds RC522_ToCard()'s ComIrqReg polling loop. This is an iteration
 *  count rather than a calibrated timeout in microseconds or milliseconds;
 *  it has not yet been tuned against real hardware. */
#define RC522_TOCARD_TIMEOUT_LOOPS  ((uint16_t)2000U)

/** Bounds RC522_CalculateCRC()'s DivIrqReg polling loop. */
#define RC522_CRC_TIMEOUT_LOOPS     ((uint8_t)0xFFU)

/** Bounds each individual HAL SPI transaction issued by this module. */
#define RC522_SPI_TIMEOUT_MS        ((uint32_t)5U)

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

/** Retains the SPI handle supplied to RC522_Init(), used internally by
 *  every subsequent register access in this module. */
static SPI_HandleTypeDef *rc522Hspi = NULL;

/* -------------------------------------------------------------------------
 * Low-level SPI register access
 * ---------------------------------------------------------------------- */

/**
 * @brief   Writes a single byte into the specified MFRC522 register.
 * @param   addr  Target register address.
 * @param   val   Byte value to be written into the target register.
 * @return  HAL_OK on success; HAL_ERROR or HAL_TIMEOUT otherwise.
 */
HAL_StatusTypeDef RC522_WriteRegister(uint8_t addr, uint8_t val)
{
    HAL_StatusTypeDef status = HAL_ERROR;
    uint8_t txAddr = RC522_SPI_ADDR_WRITE(addr);

    if (rc522Hspi != NULL)
    {
        HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_RESET);

        status = HAL_SPI_Transmit(rc522Hspi, &txAddr, 1U, RC522_SPI_TIMEOUT_MS);
        if (status == HAL_OK)
        {
            status = HAL_SPI_Transmit(rc522Hspi, &val, 1U, RC522_SPI_TIMEOUT_MS);
        }

        HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_SET);
    }

    return status;
}

/**
 * @brief   Reads and returns the current value of the specified MFRC522
 *          register.
 * @param   addr  Target register address to be read.
 * @return  The 8-bit value currently held by the target register.
 */
uint8_t RC522_ReadRegister(uint8_t addr)
{
    uint8_t txAddr = RC522_SPI_ADDR_READ(addr);
    uint8_t txDummy = 0x00U;
    uint8_t rxVal = 0U;

    if (rc522Hspi != NULL)
    {
        HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_RESET);

        /* Clocking out the address byte, then a dummy byte to receive the
         * register's value on the same full-duplex exchange. Per a.md,
         * this function does not check the HAL status, since the MFRC522
         * has no dedicated error channel of its own; SPI-level failures
         * are instead expected to surface through RC522_WriteRegister()
         * or through an unexpected VersionReg readback. */
        (void)HAL_SPI_Transmit(rc522Hspi, &txAddr, 1U, RC522_SPI_TIMEOUT_MS);
        (void)HAL_SPI_TransmitReceive(rc522Hspi, &txDummy, &rxVal, 1U, RC522_SPI_TIMEOUT_MS);

        HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_SET);
    }

    return rxVal;
}

/**
 * @brief   Sets the specified bits of a register without disturbing the
 *          remaining bits.
 * @param   addr  Target register address.
 * @param   mask  Bitmask identifying which bits shall be set to logic one.
 * @return  None.
 */
void RC522_SetBitMask(uint8_t addr, uint8_t mask)
{
    uint8_t current = RC522_ReadRegister(addr);

    (void)RC522_WriteRegister(addr, (uint8_t)(current | mask));
}

/**
 * @brief   Clears the specified bits of a register without disturbing the
 *          remaining bits.
 * @param   addr  Target register address.
 * @param   mask  Bitmask identifying which bits shall be cleared to logic
 *                zero.
 * @return  None.
 */
void RC522_ClearBitMask(uint8_t addr, uint8_t mask)
{
    uint8_t current = RC522_ReadRegister(addr);

    (void)RC522_WriteRegister(addr, (uint8_t)(current & (uint8_t)(~mask)));
}

/* -------------------------------------------------------------------------
 * Reader initialization
 * ---------------------------------------------------------------------- */

/**
 * @brief   Performs a hardware and software reset of the MFRC522, followed
 *          by the mandatory initialization register sequence.
 * @return  None.
 */
void RC522_Reset(void)
{
    /* Pulsing RC522_RST_Pin low, holding it well past the datasheet's
     * minimum reset pulse width before releasing it. */
    HAL_GPIO_WritePin(RC522_RST_GPIO_Port, RC522_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(1U);
    HAL_GPIO_WritePin(RC522_RST_GPIO_Port, RC522_RST_Pin, GPIO_PIN_SET);

    /* Allowing the crystal oscillator to stabilize after the hardware
     * reset, per the datasheet's power-up timing. */
    HAL_Delay(50U);

    (void)RC522_WriteRegister(RC522_REG_COMMAND, PCD_RESETPHASE);
    HAL_Delay(50U);

    /* Configuring the timer unit: TAuto = 1, so the timer starts
     * automatically after transmission; TPrescaler and TReload set the
     * timeout used while waiting for a card's response. */
    (void)RC522_WriteRegister(RC522_REG_TMODE, 0x8DU);
    (void)RC522_WriteRegister(RC522_REG_TPRESCALER, 0x3EU);
    (void)RC522_WriteRegister(RC522_REG_TRELOAD_L, 30U);
    (void)RC522_WriteRegister(RC522_REG_TRELOAD_H, 0U);

    (void)RC522_WriteRegister(RC522_REG_TX_ASK, 0x40U); /* Forcing 100% ASK modulation, as required by ISO/IEC 14443A. */
    (void)RC522_WriteRegister(RC522_REG_MODE, 0x3DU);   /* Setting the CRC coprocessor's preset value to 0x6363. */

    RC522_AntennaOn();
}

/**
 * @brief   Enables the antenna driver outputs (Tx1RFEn, Tx2RFEn), allowing
 *          the MFRC522 to generate the 13.56 MHz carrier required for
 *          proximity card detection.
 * @return  None.
 */
void RC522_AntennaOn(void)
{
    uint8_t txControl = RC522_ReadRegister(RC522_REG_TX_CONTROL);

    if ((txControl & RC522_TXCONTROL_RF_EN) != RC522_TXCONTROL_RF_EN)
    {
        RC522_SetBitMask(RC522_REG_TX_CONTROL, RC522_TXCONTROL_RF_EN);
    }
}

/**
 * @brief   Initializes the RC522 driver module and brings the reader into
 *          an operational state.
 * @param   hspi  Pointer to the HAL SPI handle (hspi1) configured by
 *                MX_SPI1_Init().
 * @return  HAL_OK if the reader was initialized and responds correctly on
 *          the SPI bus; HAL_ERROR otherwise.
 */
HAL_StatusTypeDef RC522_Init(SPI_HandleTypeDef *hspi)
{
    HAL_StatusTypeDef status = HAL_ERROR;
    uint8_t version;

    if (hspi != NULL)
    {
        rc522Hspi = hspi;

        RC522_Reset();

        /* Confirming SPI communication by reading VersionReg: an
         * all-zero or all-one readback indicates the bus is not
         * responding, rather than a genuine silicon version. */
        version = RC522_ReadRegister(RC522_REG_VERSION);
        if ((version != 0x00U) && (version != 0xFFU))
        {
            status = HAL_OK;
        }
    }

    return status;
}

/* -------------------------------------------------------------------------
 * Card detection and anti-collision
 * ---------------------------------------------------------------------- */

/**
 * @brief   Executes a generic communication cycle with a PICC (card):
 *          transfers command data through the FIFO, triggers execution of
 *          the specified PCD command, and retrieves the card's response.
 * @param   command   PCD command code to be executed (e.g. PCD_TRANSCEIVE).
 * @param   sendData  Pointer to the data buffer to be transmitted to the
 *                     card.
 * @param   sendLen   Number of bytes contained in sendData.
 * @param   backData  Pointer to the buffer that receives the card's
 *                     response.
 * @param   backLen   Pointer to a variable that receives the number of
 *                     valid bits/bytes written into backData.
 * @return  MI_OK on success; MI_ERR on communication, parity, or CRC
 *          failure; MI_NOCARD on timeout.
 * @note    This module issues this function exclusively with
 *          command == PCD_TRANSCEIVE; the interrupt-enable and wait masks
 *          below are chosen accordingly.
 */
uint8_t RC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen,
                      uint8_t *backData, uint16_t *backLen)
{
    uint8_t status = MI_ERR;
    uint8_t n;
    uint8_t lastBits;
    uint16_t i;
    const uint8_t irqEn = 0x77U;   /* Enabling TxIRq, RxIRq, IdleIRq, LoAlertIRq, ErrIRq, TimerIRq. */
    const uint8_t waitIRq = RC522_COMIRQ_RX_IDLE;

    (void)RC522_WriteRegister(RC522_REG_COMM_IEN, (uint8_t)(irqEn | 0x80U));
    RC522_ClearBitMask(RC522_REG_COMM_IRQ, 0x80U);
    RC522_SetBitMask(RC522_REG_FIFO_LEVEL, RC522_FIFO_FLUSH);

    (void)RC522_WriteRegister(RC522_REG_COMMAND, RC522_PCD_IDLE);

    for (i = 0U; i < sendLen; i++)
    {
        (void)RC522_WriteRegister(RC522_REG_FIFO_DATA, sendData[i]);
    }

    (void)RC522_WriteRegister(RC522_REG_COMMAND, command);
    RC522_SetBitMask(RC522_REG_BIT_FRAMING, RC522_BITFRAMING_START);

    /* Polling ComIrqReg for RxIRq/IdleIRq, bounded by a fixed iteration
     * count so that a card which never responds cannot stall the caller
     * indefinitely. */
    i = RC522_TOCARD_TIMEOUT_LOOPS;
    do
    {
        n = RC522_ReadRegister(RC522_REG_COMM_IRQ);
        i--;
    } while ((i != 0U) && ((n & RC522_COMIRQ_TIMER) == 0U) && ((n & waitIRq) == 0U));

    RC522_ClearBitMask(RC522_REG_BIT_FRAMING, RC522_BITFRAMING_START);

    if (i != 0U)
    {
        if ((RC522_ReadRegister(RC522_REG_ERROR) & RC522_ERROR_MASK) == 0U)
        {
            status = MI_OK;

            if ((n & RC522_COMIRQ_TIMER) != 0U)
            {
                status = MI_NOCARD; /* TimerIRq: no response arrived within the reader's own timing window. */
            }
            else
            {
                n = RC522_ReadRegister(RC522_REG_FIFO_LEVEL);
                lastBits = (uint8_t)(RC522_ReadRegister(RC522_REG_CONTROL) & 0x07U);

                if (lastBits != 0U)
                {
                    *backLen = (uint16_t)(((uint16_t)(n - 1U) * 8U) + lastBits);
                }
                else
                {
                    *backLen = (uint16_t)((uint16_t)n * 8U);
                }

                if (n == 0U)
                {
                    n = 1U;
                }
                if (n > 16U)
                {
                    n = 16U;
                }

                for (i = 0U; i < n; i++)
                {
                    backData[i] = RC522_ReadRegister(RC522_REG_FIFO_DATA);
                }
            }
        }
    }
    else
    {
        status = MI_NOCARD;
    }

    return status;
}

/**
 * @brief   Requests the presence of a proximity card within the antenna
 *          field.
 * @param   reqMode  Either PICC_REQIDL or PICC_REQALL.
 * @param   tagType  Pointer to a 2-byte buffer that receives the card's
 *                    ATQA value on success.
 * @return  MI_OK if a card was detected; MI_NOCARD if no card is present
 *          within the antenna field.
 */
uint8_t RC522_Request(uint8_t reqMode, uint8_t *tagType)
{
    uint8_t status;
    uint16_t backBits = 0U;

    /* Setting TxLastBits = 7: REQA/WUPA is the one ISO/IEC 14443-3 frame
     * transmitted as 7 bits rather than a whole byte. */
    (void)RC522_WriteRegister(RC522_REG_BIT_FRAMING, 0x07U);

    tagType[0] = reqMode;
    status = RC522_ToCard(PCD_TRANSCEIVE, tagType, 1U, tagType, &backBits);

    if ((status != MI_OK) || (backBits != 0x0010U))
    {
        status = MI_ERR;
    }

    return status;
}

/**
 * @brief   Computes the CRC_A checksum of the supplied data using the
 *          MFRC522's built-in CRC coprocessor.
 * @param   data       Pointer to the input data buffer.
 * @param   len        Number of bytes in the input data buffer.
 * @param   crcResult  Pointer to a 2-byte buffer that receives the
 *                      resulting CRC_A value (little-endian).
 * @return  None.
 */
void RC522_CalculateCRC(uint8_t *data, uint8_t len, uint8_t *crcResult)
{
    uint8_t n;
    uint8_t i;

    RC522_ClearBitMask(RC522_REG_DIV_IRQ, RC522_DIVIRQ_CRC);
    RC522_SetBitMask(RC522_REG_FIFO_LEVEL, RC522_FIFO_FLUSH);

    for (i = 0U; i < len; i++)
    {
        (void)RC522_WriteRegister(RC522_REG_FIFO_DATA, data[i]);
    }

    (void)RC522_WriteRegister(RC522_REG_COMMAND, PCD_CALCCRC);

    /* Polling DivIrqReg for CRCIRq, bounded by a fixed iteration count. */
    n = RC522_CRC_TIMEOUT_LOOPS;
    do
    {
        i = RC522_ReadRegister(RC522_REG_DIV_IRQ);
        n--;
    } while ((n != 0U) && ((i & RC522_DIVIRQ_CRC) == 0U));

    crcResult[0] = RC522_ReadRegister(RC522_REG_CRC_RESULT_L);
    crcResult[1] = RC522_ReadRegister(RC522_REG_CRC_RESULT_M);
}

/**
 * @brief   Executes the anti-collision command for a single cascade level
 *          and validates the returned BCC checksum.
 * @param   cascadeCmd  Cascade-level anti-collision command (PICC_ANTICOLL
 *                       for cascade level 1, RC522_PICC_ANTICOLL_CL2 for
 *                       cascade level 2).
 * @param   out         Pointer to a 5-byte buffer that receives 4 UID
 *                       bytes followed by the BCC byte.
 * @return  MI_OK on success; MI_ERR on communication failure or BCC
 *          mismatch.
 */
static uint8_t RC522_AnticollLevel(uint8_t cascadeCmd, uint8_t *out)
{
    uint8_t status;
    uint8_t sendBuf[2];
    uint16_t backBits = 0U;
    uint8_t bcc;
    uint8_t i;

    sendBuf[0] = cascadeCmd;
    sendBuf[1] = 0x20U;

    status = RC522_ToCard(PCD_TRANSCEIVE, sendBuf, 2U, out, &backBits);

    if (status == MI_OK)
    {
        bcc = 0U;
        for (i = 0U; i < 4U; i++)
        {
            bcc ^= out[i];
        }

        if (bcc != out[4])
        {
            status = MI_ERR;
        }
    }

    return status;
}

/**
 * @brief   Performs the ISO/IEC 14443-3 anti-collision procedure and
 *          retrieves the UID of a card present in the antenna field.
 * @param   uidBuf  Pointer to a buffer of at least 7 bytes that receives
 *                   the card's UID.
 * @param   uidLen  Pointer to a variable that receives the actual UID
 *                   length in bytes.
 * @return  MI_OK on success; MI_ERR if the BCC checksum verification
 *          fails or a communication error occurs.
 * @note    Evaluates the cascade tag byte (0x88) returned at cascade
 *          level 1 to determine whether a second cascade level is
 *          required, rather than assuming a fixed 4-byte UID; when a
 *          second level is required, this function performs the
 *          intermediate cascade-level-1 select internally so that
 *          RC522_SelectTag() only has to complete the final level.
 */
uint8_t RC522_Anticoll(uint8_t *uidBuf, uint8_t *uidLen)
{
    uint8_t status;
    uint8_t cl1[5] = {0};
    uint8_t cl2[5] = {0};
    uint8_t sak = 0U;

    (void)RC522_WriteRegister(RC522_REG_BIT_FRAMING, 0x00U);

    status = RC522_AnticollLevel(PICC_ANTICOLL, cl1);

    if (status == MI_OK)
    {
        if (cl1[0] == RC522_CASCADE_TAG)
        {
            /* Selecting cascade level 1 first, advancing the card into
             * cascade level 2 before its remaining UID bytes can be
             * read, per ISO/IEC 14443-3. */
            status = RC522_SelectTag(cl1, 4U, &sak);

            if (status == MI_OK)
            {
                status = RC522_AnticollLevel(RC522_PICC_ANTICOLL_CL2, cl2);
            }

            if (status == MI_OK)
            {
                uidBuf[0] = cl1[1];
                uidBuf[1] = cl1[2];
                uidBuf[2] = cl1[3];
                uidBuf[3] = cl2[0];
                uidBuf[4] = cl2[1];
                uidBuf[5] = cl2[2];
                uidBuf[6] = cl2[3];
                *uidLen = 7U;
            }
        }
        else
        {
            uidBuf[0] = cl1[0];
            uidBuf[1] = cl1[1];
            uidBuf[2] = cl1[2];
            uidBuf[3] = cl1[3];
            *uidLen = 4U;
        }
    }

    return status;
}

/**
 * @brief   Selects a card identified by the given UID, completing the
 *          activation sequence required before further communication.
 * @param   uidBuf  Pointer to the UID previously obtained via
 *                   RC522_Anticoll().
 * @param   uidLen  Length, in bytes, of the UID pointed to by uidBuf.
 * @param   sak     Pointer to a variable that receives the card's Select
 *                   Acknowledge (SAK) byte.
 * @return  MI_OK on success; MI_ERR otherwise.
 * @note    For a 7-byte UID, selects cascade level 2 using uidBuf[3..6],
 *          since cascade level 1 has already been selected internally by
 *          RC522_Anticoll().
 */
uint8_t RC522_SelectTag(uint8_t *uidBuf, uint8_t uidLen, uint8_t *sak)
{
    uint8_t status;
    uint8_t sendBuf[9];
    uint8_t backBuf[3] = {0};
    uint16_t backBits = 0U;
    uint8_t cascadeCmd;
    uint8_t *cascadeUid;

    if (uidLen == 7U)
    {
        cascadeCmd = RC522_PICC_ANTICOLL_CL2;
        cascadeUid = &uidBuf[3];
    }
    else
    {
        cascadeCmd = PICC_SELECTTAG;
        cascadeUid = &uidBuf[0];
    }

    sendBuf[0] = cascadeCmd;
    sendBuf[1] = 0x70U;
    sendBuf[2] = cascadeUid[0];
    sendBuf[3] = cascadeUid[1];
    sendBuf[4] = cascadeUid[2];
    sendBuf[5] = cascadeUid[3];
    sendBuf[6] = (uint8_t)(cascadeUid[0] ^ cascadeUid[1] ^ cascadeUid[2] ^ cascadeUid[3]);
    RC522_CalculateCRC(sendBuf, 7U, &sendBuf[7]);

    status = RC522_ToCard(PCD_TRANSCEIVE, sendBuf, 9U, backBuf, &backBits);

    if ((status == MI_OK) && (backBits == 0x0018U))
    {
        *sak = backBuf[0];
    }
    else
    {
        status = MI_ERR;
    }

    return status;
}

/**
 * @brief   Instructs the currently selected card to enter the HALT state.
 * @return  None.
 */
void RC522_Halt(void)
{
    uint8_t sendBuf[4];
    uint8_t backBuf[1] = {0};
    uint16_t backBits = 0U;

    sendBuf[0] = PICC_HALT;
    sendBuf[1] = 0x00U;
    RC522_CalculateCRC(sendBuf, 2U, &sendBuf[2]);

    /* Discarding the return value: a halted card is not expected to
     * acknowledge HLTA, so a timeout here is the normal outcome rather
     * than a failure. */
    (void)RC522_ToCard(PCD_TRANSCEIVE, sendBuf, 4U, backBuf, &backBits);
}

/* -------------------------------------------------------------------------
 * High-level application interface
 * ---------------------------------------------------------------------- */

/**
 * @brief   Performs a complete card-read cycle (request, anti-collision,
 *          select, halt) and returns the UID of any card detected.
 * @param   uidBuf  Pointer to a buffer of at least 7 bytes that receives
 *                   the detected card's UID.
 * @param   uidLen  Pointer to a variable that receives the UID length, in
 *                   bytes.
 * @return  MI_OK if a card was successfully read; MI_NOCARD if no card is
 *          present; MI_ERR if a communication or checksum error occurred
 *          during the read cycle.
 */
uint8_t RC522_ReadCardUID(uint8_t *uidBuf, uint8_t *uidLen)
{
    uint8_t status;
    uint8_t tagType[2] = {0};
    uint8_t sak = 0U;

    status = RC522_Request(PICC_REQIDL, tagType);

    if (status == MI_OK)
    {
        status = RC522_Anticoll(uidBuf, uidLen);
    }

    if (status == MI_OK)
    {
        status = RC522_SelectTag(uidBuf, *uidLen, &sak);
    }

    if (status == MI_OK)
    {
        RC522_Halt();
    }

    return status;
}
