#include "oled.h"
#include <string.h>

#define OLED_ADDRESS_LOW   0x3CU
#define OLED_ADDRESS_HIGH  0x3DU
#define OLED_TIMEOUT_MS    25U

static I2C_HandleTypeDef *oled_i2c;
static uint16_t oled_address;
static uint8_t framebuffer[OLED_WIDTH * OLED_PAGES];
static uint8_t cursor_x;
static uint8_t cursor_page;
static bool oled_ready;

static const uint8_t digit_font[10][5] = {
  {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU},
  {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U},
  {0x42U, 0x61U, 0x51U, 0x49U, 0x46U},
  {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U},
  {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U},
  {0x27U, 0x45U, 0x45U, 0x45U, 0x39U},
  {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U},
  {0x01U, 0x71U, 0x09U, 0x05U, 0x03U},
  {0x36U, 0x49U, 0x49U, 0x49U, 0x36U},
  {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU}
};

static const uint8_t upper_font[26][5] = {
  {0x7EU,0x11U,0x11U,0x11U,0x7EU}, {0x7FU,0x49U,0x49U,0x49U,0x36U},
  {0x3EU,0x41U,0x41U,0x41U,0x22U}, {0x7FU,0x41U,0x41U,0x22U,0x1CU},
  {0x7FU,0x49U,0x49U,0x49U,0x41U}, {0x7FU,0x09U,0x09U,0x09U,0x01U},
  {0x3EU,0x41U,0x49U,0x49U,0x7AU}, {0x7FU,0x08U,0x08U,0x08U,0x7FU},
  {0x00U,0x41U,0x7FU,0x41U,0x00U}, {0x20U,0x40U,0x41U,0x3FU,0x01U},
  {0x7FU,0x08U,0x14U,0x22U,0x41U}, {0x7FU,0x40U,0x40U,0x40U,0x40U},
  {0x7FU,0x02U,0x0CU,0x02U,0x7FU}, {0x7FU,0x04U,0x08U,0x10U,0x7FU},
  {0x3EU,0x41U,0x41U,0x41U,0x3EU}, {0x7FU,0x09U,0x09U,0x09U,0x06U},
  {0x3EU,0x41U,0x51U,0x21U,0x5EU}, {0x7FU,0x09U,0x19U,0x29U,0x46U},
  {0x46U,0x49U,0x49U,0x49U,0x31U}, {0x01U,0x01U,0x7FU,0x01U,0x01U},
  {0x3FU,0x40U,0x40U,0x40U,0x3FU}, {0x1FU,0x20U,0x40U,0x20U,0x1FU},
  {0x3FU,0x40U,0x38U,0x40U,0x3FU}, {0x63U,0x14U,0x08U,0x14U,0x63U},
  {0x07U,0x08U,0x70U,0x08U,0x07U}, {0x61U,0x51U,0x49U,0x45U,0x43U}
};

static HAL_StatusTypeDef WriteCommand(uint8_t command)
{
  const uint8_t packet[2] = {0x00U, command};
  return HAL_I2C_Master_Transmit(oled_i2c, oled_address, (uint8_t *)packet,
                                 sizeof(packet), OLED_TIMEOUT_MS);
}

static void GetGlyph(char character, uint8_t glyph[5])
{
  memset(glyph, 0, 5U);
  if ((character >= 'a') && (character <= 'z')) character -= ('a' - 'A');
  if ((character >= '0') && (character <= '9')) {
    memcpy(glyph, digit_font[(uint8_t)(character - '0')], 5U);
  } else if ((character >= 'A') && (character <= 'Z')) {
    memcpy(glyph, upper_font[(uint8_t)(character - 'A')], 5U);
  } else if (character == ':') {
    glyph[2] = 0x36U;
  } else if (character == '.') {
    glyph[2] = 0x60U;
  } else if (character == '-') {
    glyph[1] = glyph[2] = glyph[3] = 0x08U;
  } else if (character == '+') {
    glyph[1] = 0x08U; glyph[2] = 0x1CU; glyph[3] = 0x08U;
  } else if (character == '%') {
    glyph[0] = 0x63U; glyph[1] = 0x13U; glyph[2] = 0x08U;
    glyph[3] = 0x64U; glyph[4] = 0x63U;
  }
}

HAL_StatusTypeDef OLED_Init(I2C_HandleTypeDef *i2c)
{
  const uint8_t addresses[] = {OLED_ADDRESS_LOW, OLED_ADDRESS_HIGH};
  oled_ready = false;
  oled_address = 0U;
  if (i2c == NULL) return HAL_ERROR;
  oled_i2c = i2c;
  for (uint32_t index = 0U; index < 2U; ++index) {
    const uint16_t candidate = (uint16_t)(addresses[index] << 1U);
    if (HAL_I2C_IsDeviceReady(oled_i2c, candidate, 2U, OLED_TIMEOUT_MS) == HAL_OK) {
      oled_address = candidate;
      break;
    }
  }
  if (oled_address == 0U) return HAL_ERROR;

  const uint8_t commands[] = {
    0xAEU, 0xD5U, 0x80U, 0xA8U, 0x3FU, 0xD3U, 0x00U, 0x40U,
    0x8DU, 0x14U, 0x20U, 0x02U, 0xA1U, 0xC8U, 0xDAU, 0x12U,
    0x81U, 0xCFU, 0xD9U, 0xF1U, 0xDBU, 0x40U, 0xA4U, 0xA6U, 0xAFU
  };
  for (uint32_t index = 0U; index < sizeof(commands); ++index) {
    if (WriteCommand(commands[index]) != HAL_OK) return HAL_ERROR;
  }
  OLED_Clear();
  oled_ready = true;
  return HAL_OK;
}

void OLED_Clear(void)
{
  memset(framebuffer, 0, sizeof(framebuffer));
  cursor_x = 0U;
  cursor_page = 0U;
}

void OLED_SetCursor(uint8_t x, uint8_t page)
{
  cursor_x = x;
  cursor_page = page;
}

void OLED_WriteString(const char *text)
{
  uint8_t glyph[5];
  if ((text == NULL) || (cursor_page >= OLED_PAGES)) return;
  while ((*text != '\0') && ((uint16_t)cursor_x + 6U <= OLED_WIDTH)) {
    GetGlyph(*text++, glyph);
    for (uint32_t column = 0U; column < 5U; ++column) {
      framebuffer[(uint16_t)cursor_page * OLED_WIDTH + cursor_x++] = glyph[column];
    }
    framebuffer[(uint16_t)cursor_page * OLED_WIDTH + cursor_x++] = 0U;
  }
}

HAL_StatusTypeDef OLED_UpdatePage(uint8_t page)
{
  uint8_t packet[17];
  if (!oled_ready || (page >= OLED_PAGES)) return HAL_ERROR;
  if ((WriteCommand((uint8_t)(0xB0U + page)) != HAL_OK) ||
      (WriteCommand(0x00U) != HAL_OK) || (WriteCommand(0x10U) != HAL_OK)) {
    oled_ready = false;
    return HAL_ERROR;
  }
  packet[0] = 0x40U;
  for (uint32_t offset = 0U; offset < OLED_WIDTH; offset += 16U) {
    memcpy(&packet[1], &framebuffer[(uint16_t)page * OLED_WIDTH + offset], 16U);
    if (HAL_I2C_Master_Transmit(oled_i2c, oled_address, packet, sizeof(packet),
                                OLED_TIMEOUT_MS) != HAL_OK) {
      oled_ready = false;
      return HAL_ERROR;
    }
  }
  return HAL_OK;
}

bool OLED_IsReady(void)
{
  return oled_ready;
}

uint8_t OLED_GetAddress(void)
{
  return (uint8_t)(oled_address >> 1U);
}
