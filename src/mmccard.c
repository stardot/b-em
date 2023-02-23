#include "b-em.h"
#include "mmccard.h"

enum mmcstate {
    MMC_IDLE,
    MMC_RECV_ARGS,
    MMC_END_CMD,
    MMC_CARD_ID,
    MMC_SEND_ARGS,
    MMC_READ_TOKEN,
    MMC_READ_BYTES,
    MMC_WRITE_TOKEN,
    MMC_WRITE_BYTES,
    MMC_WRITE_FINISH
};

static enum mmcstate mmc_state = MMC_IDLE;

char *mmccard_fn = NULL;
static FILE *mmc_fp = NULL;
static off_t mmc_size = 0;
static bool mmc_wprot = false;
static uint8_t mmc_shiftreg = 0xff;
static unsigned mmc_count = 0;
static uint8_t mmc_cmd, mmc_args[7];
static unsigned char mmc_buffer[0x200];
static off_t mmc_block_len = 0x200;
static bool mmc_sdhc_mode = false;

// Card ID - I'm familiar with these numbers
static const unsigned char CardID[] = {
        0xff, 0xfe, 0x01, 0x00, 0x00,
        0x33, 0x32, 0xff, 0xff, 0xff,
        0xff, 0xff, 0x19, 0x88, 0x4f,
        0x7a, 0x34, 0xff, 0x6a, 0xca
};

uint8_t mmccard_read(void)
{
    log_debug("mmccard: read, shiftreg=%02X", mmc_shiftreg);
    return mmc_shiftreg;
}

void mmccard_write(uint8_t byte)
{
    log_debug("mmccard: write, byte=%02X", byte);
    if (mmc_fp && mmc_size) {
        switch(mmc_state) {
            case MMC_IDLE:
                if (byte != 0xff) {
                    mmc_cmd = byte;
                    mmc_count = 0;
                    mmc_state = MMC_RECV_ARGS;
                }
                break;
            case MMC_RECV_ARGS:
                mmc_args[mmc_count++] = byte;
                if (mmc_count == 7) {
                    off_t address;
                    log_debug("mmccard: executing cmd %02X", mmc_cmd);
                    switch(mmc_cmd) {
                        case 0x40: // Reset card.
                            mmc_shiftreg = 0x01;
                            mmc_state = MMC_IDLE;
                            break;
                        case 0x69:
                            if (mmc_args[0] & 0x40)
                                mmc_sdhc_mode = true;
                            // FALL THROUGH.
                        case 0x41: // Initialise card.
                        case 0x77:
                            mmc_shiftreg = 0x00;
                            mmc_state = MMC_IDLE;
                            break;
                        case 0x48: // MMC_IF_COND, used to detect SDHC.
                            mmc_shiftreg = 0x01;
                            mmc_count = 0;
                            mmc_state = MMC_SEND_ARGS;
                            break;
                        case 0x4a: // Card ID.
                            mmc_shiftreg = 0;
                            mmc_count = 0;
                            mmc_state = MMC_CARD_ID;
                            break;
                        case 0x50: // Set block length.
                            address = (mmc_args[0] << 24) | (mmc_args[1] << 16) | (mmc_args[2] << 8) | mmc_args[3];
                            log_debug("mmccard: set block length to %llx", address);
                            if (address > 0 && address <= sizeof(mmc_buffer)) {
                                mmc_shiftreg = 0x00;
                                mmc_block_len = address;
                            } else
                                mmc_shiftreg = 0x40;
                            mmc_state = MMC_IDLE;
                            break;
                        case 0x51: // Read sector.
                            address = (mmc_args[0] << 24) | (mmc_args[1] << 16) | (mmc_args[2] << 8) | mmc_args[3];
                            if (mmc_sdhc_mode)
                                address *= 0x200;
                            log_debug("mmccard: read from %llx", address);
                            if (address < mmc_size) {
                                if (!fseek(mmc_fp, address, SEEK_SET) && fread(mmc_buffer, mmc_block_len, 1, mmc_fp) == 1) {
                                    mmc_shiftreg = 0x00;
                                    mmc_state = MMC_READ_TOKEN;
                                }
                                else {
                                    mmc_shiftreg = 0xff; // read error
                                    mmc_state = MMC_IDLE;
                                }
                            }
                            else {
                                mmc_shiftreg = 0x40; // address out of range.
                                mmc_state = MMC_IDLE;
                            }
                            break;
                        case 0x58: // Write sector.
                            address = (mmc_args[0] << 24) | (mmc_args[1] << 16) | (mmc_args[2] << 8) | mmc_args[3];
                            if (mmc_sdhc_mode)
                                address *= 0x200;
                            log_debug("mmccard: write to %llx", address);
                            if (mmc_wprot) {
                                mmc_shiftreg = 0xff;
                                mmc_state = MMC_IDLE;
                            }
                            else if (!fseek(mmc_fp, address, SEEK_SET)) {
                                mmc_count = 0;
                                mmc_shiftreg = 0;
                                mmc_state = MMC_WRITE_TOKEN;
                            }
                            else {
                                mmc_shiftreg = 0x40;
                                mmc_state = MMC_IDLE;
                            }
                            break;
                        case 0x7a:
                            mmc_shiftreg = 0;
                            mmc_args[0] = 0x40;
                            mmc_count = 0;
                            mmc_state = MMC_SEND_ARGS;
                            break;
                        default:
                            mmc_shiftreg = 0xff;
                            mmc_state = MMC_IDLE;
                    }
                }
                break;
            case MMC_END_CMD:
                mmc_shiftreg = 0xff;
                mmc_state = MMC_IDLE;
                break;
            case MMC_SEND_ARGS:
                if (mmc_count >= 4) {
                    mmc_shiftreg = 0;
                    mmc_state = MMC_IDLE;
                }
                else
                    mmc_shiftreg = mmc_args[mmc_count++];
                break;
            case MMC_CARD_ID:
                mmc_shiftreg = CardID[mmc_count++];
                log_debug("mmccard: cardid, byte=%02X, count=%d", mmc_shiftreg, mmc_count);
                if (mmc_count >= sizeof(CardID))
                    mmc_state = MMC_END_CMD;
                break;
            case MMC_READ_TOKEN:
                mmc_shiftreg = 0xfe;
                mmc_count = 0;
                mmc_state = MMC_READ_BYTES;
                break;
            case MMC_READ_BYTES:
                mmc_shiftreg = mmc_buffer[mmc_count++];
                if (mmc_count >= mmc_block_len)
                    mmc_state = MMC_END_CMD;
                break;
            case MMC_WRITE_TOKEN:
                if (byte == 0xfe)
                    mmc_state = MMC_WRITE_BYTES;
                break;
            case MMC_WRITE_BYTES:
                mmc_buffer[mmc_count++] = byte;
                if (mmc_count >= mmc_block_len) {
                    if (fwrite(mmc_buffer, mmc_block_len, 1, mmc_fp) == 1) {
                        fflush(mmc_fp); // Guard against a real card being removed.
                        mmc_shiftreg = 0x05;
                        mmc_count = 0;
                        mmc_state = MMC_WRITE_FINISH;
                    }
                    else {
                        mmc_shiftreg = 0xff;
                        mmc_state = MMC_IDLE;
                    }
                }
                break;
            case MMC_WRITE_FINISH:
                if (++mmc_count == 16) {
                    mmc_shiftreg = 0xff;
                    mmc_state = MMC_IDLE;
                }
        }
    }
}

void mmccard_load(char *fn)
{
    mmccard_fn = fn;
    mmc_wprot = false;
    FILE *fp = fopen(fn, "rb+");
    if (!fp) {
        fp = fopen(fn, "rb");
        if (!fp) {
            log_error("unable to open MMC card image %s: %s", fn, strerror(errno));
            return;
        }
        mmc_wprot = true;
    }
    fseek(fp, 0, SEEK_END);
    mmc_size = ftell(fp);
    mmc_fp = fp;
    log_debug("mmcard: %s loaded, size=%lld", fn, mmc_size);
}

void mmccard_eject(void)
{
    if (mmc_fp) {
        fclose(mmc_fp);
        mmc_fp = NULL;
    }
    if (mmccard_fn) {
        free(mmccard_fn);
        mmccard_fn = NULL;
    }
}
