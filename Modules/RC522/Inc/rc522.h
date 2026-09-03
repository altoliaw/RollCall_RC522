/**
 * @file    rc522.h
 * @brief   Public interface of the MFRC522 RFID reader driver module.
 *
 * This header declares the functions required to initialize the MFRC522
 * reader over SPI1, detect ISO/IEC 14443 Type A proximity cards, and
 * retrieve their unique identifier (UID) for the RollCall_RC522 attendance
 * terminal. The corresponding definitions in rc522.c are intentionally
 * left unimplemented at this stage.
 *
 * @note    Section Firmware Receiver-Side (RC522 Card-Read Flow) Architecture 
 * Diagram
 */

#ifndef RC522_INC_RC522_H_
#define RC522_INC_RC522_H_

#include "../../../Core/Inc/main.h"

/* Exported status codes --------------------------------------------------- */
#define MI_OK       ((uint8_t)0x00U) /**< Operation completed successfully. */
#define MI_ERR      ((uint8_t)0x01U) /**< Operation failed (communication, parity, or checksum error). */
#define MI_NOCARD   ((uint8_t)0x02U) /**< No card was detected within the antenna field. */

/* Exported PICC (card-side) command codes --------------------------------- */
#define PICC_REQIDL     ((uint8_t)0x26U) /**< REQA, requesting idle (non-halted) cards only. */
#define PICC_REQALL     ((uint8_t)0x52U) /**< WUPA, requesting all cards, including halted ones. */
#define PICC_ANTICOLL   ((uint8_t)0x93U) /**< Performing anti-collision, cascade level 1. */
#define PICC_SELECTTAG  ((uint8_t)0x93U) /**< Selecting a tag, cascade level 1. */
#define PICC_HALT       ((uint8_t)0x50U) /**< HLTA, instructing the selected card to enter the HALT state. */

/* Exported PCD (reader-side) command codes -------------------------------- */
#define PCD_RESETPHASE  ((uint8_t)0x0FU) /**< Performing a soft reset of the MFRC522. */
#define PCD_TRANSCEIVE  ((uint8_t)0x0CU) /**< Transmitting FIFO data and automatically receiving the reply. */
#define PCD_CALCCRC     ((uint8_t)0x03U) /**< Activating the internal CRC coprocessor. */

/* -------------------------------------------------------------------------
 * Low-level SPI register access
 * ---------------------------------------------------------------------- */

/**
 * @brief   Writes a single byte into the specified MFRC522 register.
 * @param   addr  [uint8_t] Target register address, as defined in the MFRC522
 *                datasheet, Section 9.3 (Register Overview).
 * @param   val   [uint8_t] Byte value to be written into the target register.
 * @return  [HAL_StatusTypeDef] HAL_OK if the SPI transaction completed successfully;
 *          HAL_ERROR or HAL_TIMEOUT otherwise, as propagated from the
 *          underlying HAL_SPI_Transmit() call.
 * @note    The implementation shall bracket the transaction between an
 *          active-low assertion and de-assertion of RC522_CS_Pin.
 */
HAL_StatusTypeDef RC522_WriteRegister(uint8_t addr, uint8_t val);

/**
 * @brief   Reads and returns the current value of the specified MFRC522
 *          register.
 * @param   addr  [uint8_t] Target register address to be read.
 * @return  [uint8_t] The 8-bit value currently held by the target register.
 * @note    The most significant bit of the address byte shall be set to
 *          logic one to indicate a read access, per the MFRC522 SPI
 *          protocol.
 */
uint8_t RC522_ReadRegister(uint8_t addr);

/**
 * @brief   Sets the specified bits of a register without disturbing the
 *          remaining bits.
 * @param   addr  [uint8_t] Target register address.
 * @param   mask  [uint8_t] Bitmask identifying which bits shall be set to logic one.
 * @return  None
 */
void RC522_SetBitMask(uint8_t addr, uint8_t mask);

/**
 * @brief   Clears the specified bits of a register without disturbing the
 *          remaining bits.
 * @param   addr  [uint8_t] Target register address.
 * @param   mask  [uint8_t] Bitmask identifying which bits shall be cleared to logic
 *                zero.
 * @return  None
 */
void RC522_ClearBitMask(uint8_t addr, uint8_t mask);

/* -------------------------------------------------------------------------
 * Reader initialization
 * ---------------------------------------------------------------------- */

/**
 * @brief   Performs a hardware and software reset of the MFRC522, followed
 *          by the mandatory initialization register sequence.
 * @return  None
 * @note    This is the only function permitted to directly drive
 *          RC522_RST_Pin; no other function in this module shall access
 *          it, in order to preserve a single, consistent reset sequence.
 */
void RC522_Reset(void);

/**
 * @brief   Enables the antenna driver outputs (Tx1RFEn, Tx2RFEn), allowing
 *          the MFRC522 to generate the 13.56 MHz carrier required for
 *          proximity card detection.
 * @return  None
 */
void RC522_AntennaOn(void);

/**
 * @brief   Initializes the RC522 driver module and brings the reader into
 *          an operational state.
 * @param   hspi  [SPI_HandleTypeDef*] A pointer to the HAL SPI handle (hspi1) configured by
 *                MX_SPI1_Init(), retained internally by this module for
 *                all subsequent SPI transactions.
 * @return  [HAL_StatusTypeDef] HAL_OK if the reader was initialized and responds correctly on
 *          the SPI bus; HAL_ERROR otherwise.
 * @note    Callers shall invoke this function exactly once, prior to any
 *          other function of this module, typically from the
 *          USER CODE BEGIN 2 section of main().
 */
HAL_StatusTypeDef RC522_Init(SPI_HandleTypeDef *hspi);

/* -------------------------------------------------------------------------
 * Card detection and anti-collision
 * ---------------------------------------------------------------------- */

/**
 * @brief   Executes a generic communication cycle with a PICC (card):
 *          transfers command data through the FIFO, triggers execution of
 *          the specified PCD command, and retrieves the card's response.
 * @param   command   [uint8_t] PCD command code to be executed (e.g., PCD_TRANSCEIVE).
 * @param   sendData  [uint8_t*] A pointer to the data buffer to be transmitted to the
 *                     card.
 * @param   sendLen   [uint8_t] Number of bytes contained in sendData.
 * @param   backData  [uint8_t*] A pointer to the buffer that receives the card's
 *                     response.
 * @param   backLen   [uint16_t*] A pointer to a variable that receives the number of
 *                     valid bits/bytes written into backData.
 * @return  [uint8_t] MI_OK on success; MI_ERR on communication, parity, or CRC
 *          failure; MI_NOCARD on timeout (no response detected within the
 *          expected window).
 */
uint8_t RC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen,
                      uint8_t *backData, uint16_t *backLen);

/**
 * @brief   Requests the presence of a proximity card within the antenna
 *          field.
 * @param   reqMode  [uint8_t] Either PICC_REQIDL (idle cards only) or PICC_REQALL
 *                    (idle and halted cards).
 * @param   tagType  [uint8_t*] A pointer to a 2-byte buffer that receives the card's
 *                    ATQA (Answer To Request, Type A) value on success.
 * @return  [uint8_t] MI_OK if a card was detected; MI_NOCARD if no card is present
 *          within the antenna field.
 */
uint8_t RC522_Request(uint8_t reqMode, uint8_t *tagType);

/**
 * @brief   Computes the CRC_A checksum of the supplied data using the
 *          MFRC522's built-in CRC coprocessor.
 * @param   data       [uint8_t*] A pointer to the input data buffer.
 * @param   len        [uint8_t] Number of bytes in the input data buffer.
 * @param   crcResult  [uint8_t*] A pointer to a 2-byte buffer that receives the
 *                      resulting CRC_A value (little-endian).
 * @return  None.
 */
void RC522_CalculateCRC(uint8_t *data, uint8_t len, uint8_t *crcResult);

/**
 * @brief   Performs the ISO/IEC 14443-3 anti-collision procedure and
 *          retrieves the UID of a card present in the antenna field.
 * @param   uidBuf  [uint8_t*] A pointer to a buffer of at least 7 bytes that receives
 *                   the card's UID.
 * @param   uidLen  [uint8_t*] A pointer to a variable that receives the actual UID
 *                   length in bytes (4 for a single cascade level, 7 for a
 *                   card requiring cascade level 2).
 * @return  [uint8_t] MI_OK on success; MI_ERR if the BCC checksum verification
 *          fails or a communication error occurs.
 * @note    The implementation shall evaluate the cascade tag byte (0x88)
 *          to determine whether a second cascade level is required,
 *          rather than assuming a fixed 4-byte UID.
 */
uint8_t RC522_Anticoll(uint8_t *uidBuf, uint8_t *uidLen);

/**
 * @brief   Selects a card identified by the given UID, completing the
 *          activation sequence required before further communication.
 * @param   uidBuf  [uint8_t*] Pointer to the UID previously obtained via
 *                   RC522_Anticoll().
 * @param   uidLen  [uint8_t] Length, in bytes, of the UID pointed to by uidBuf.
 * @param   sak     [uint8_t*] Pointer to a variable that receives the card's Select
 *                   Acknowledge (SAK) byte.
 * @return  [uint8_t] MI_OK on success; MI_ERR otherwise.
 */
uint8_t RC522_SelectTag(uint8_t *uidBuf, uint8_t uidLen, uint8_t *sak);

/**
 * @brief   Instructs the currently selected card to enter the HALT state.
 * @return  None
 * @note    This reduces, but does not eliminate, repeated detections of a
 *          card left resting on the reader; a complete debounce policy
 *          shall additionally be implemented by the caller, as noted in
 *          a.md, Section 4 ("Repeated Card-Read / Debounce Logic").
 */
void RC522_Halt(void);

/* -------------------------------------------------------------------------
 * High-level application interface
 * ---------------------------------------------------------------------- */

/**
 * @brief   Performs a complete card-read cycle (request, anti-collision,
 *          select, halt) and returns the UID of any card detected.
 * @param   uidBuf  [uint8_t*] Pointer to a buffer of at least 7 bytes that receives
 *                   the detected card's UID.
 * @param   uidLen  [uint8_t*] Pointer to a variable that receives the UID length, in
 *                   bytes.
 * @return  [uint8_t] MI_OK if a card was successfully read; MI_NOCARD if no card is
 *          present; MI_ERR if a communication or checksum error occurred
 *          during the read cycle.
 * @note    This is the single entry point intended for repeated polling
 *          from the main application loop.
 */
uint8_t RC522_ReadCardUID(uint8_t *uidBuf, uint8_t *uidLen);

#endif /* RC522_INC_RC522_H_ */
