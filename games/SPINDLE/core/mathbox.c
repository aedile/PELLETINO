/*
 * mathbox.c - see mathbox.h. Ported from MAME's mathbox.cpp (Eric Smith, BSD-3-Clause).
 * The register names and the flow are kept as they are there, because the arithmetic only
 * makes sense read against the original.
 */
#include "mathbox.h"

#define REG0 mb->reg[0x00]
#define REG1 mb->reg[0x01]
#define REG2 mb->reg[0x02]
#define REG3 mb->reg[0x03]
#define REG4 mb->reg[0x04]
#define REG5 mb->reg[0x05]
#define REG6 mb->reg[0x06]
#define REG7 mb->reg[0x07]
#define REG8 mb->reg[0x08]
#define REG9 mb->reg[0x09]
#define REGa mb->reg[0x0a]
#define REGb mb->reg[0x0b]
#define REGc mb->reg[0x0c]
#define REGd mb->reg[0x0d]
#define REGe mb->reg[0x0e]
#define REGf mb->reg[0x0f]

void mathbox_reset(mathbox_t *mb)
{
    for (int i = 0; i < 16; i++) mb->reg[i] = 0;
    mb->result = 0;
}

void mathbox_go(mathbox_t *mb, uint8_t opcode, uint8_t data)
{
    int32_t mb_temp;
    int16_t mb_q;
    int msb;

    switch (opcode) {
    case 0x00: mb->result = REG0 = (int16_t)((REG0 & 0xff00) | data);        break;
    case 0x01: mb->result = REG0 = (int16_t)((REG0 & 0x00ff) | (data << 8)); break;
    case 0x02: mb->result = REG1 = (int16_t)((REG1 & 0xff00) | data);        break;
    case 0x03: mb->result = REG1 = (int16_t)((REG1 & 0x00ff) | (data << 8)); break;
    case 0x04: mb->result = REG2 = (int16_t)((REG2 & 0xff00) | data);        break;
    case 0x05: mb->result = REG2 = (int16_t)((REG2 & 0x00ff) | (data << 8)); break;
    case 0x06: mb->result = REG3 = (int16_t)((REG3 & 0xff00) | data);        break;
    case 0x07: mb->result = REG3 = (int16_t)((REG3 & 0x00ff) | (data << 8)); break;
    case 0x08: mb->result = REG4 = (int16_t)((REG4 & 0xff00) | data);        break;
    case 0x09: mb->result = REG4 = (int16_t)((REG4 & 0x00ff) | (data << 8)); break;

    case 0x0a: mb->result = REG5 = (int16_t)((REG5 & 0xff00) | data);        break;
        /* no function loads the low half of REG5 without also computing something */

    case 0x0c: mb->result = REG6 = data; break;
        /* and none loads the high half of REG6 */

    case 0x15: mb->result = REG7 = (int16_t)((REG7 & 0xff00) | data);        break;
    case 0x16: mb->result = REG7 = (int16_t)((REG7 & 0x00ff) | (data << 8)); break;
    case 0x1a: mb->result = REG8 = (int16_t)((REG8 & 0xff00) | data);        break;
    case 0x1b: mb->result = REG8 = (int16_t)((REG8 & 0x00ff) | (data << 8)); break;
    case 0x0d: mb->result = REGa = (int16_t)((REGa & 0xff00) | data);        break;
    case 0x0e: mb->result = REGa = (int16_t)((REGa & 0x00ff) | (data << 8)); break;
    case 0x0f: mb->result = REGb = (int16_t)((REGb & 0xff00) | data);        break;
    case 0x10: mb->result = REGb = (int16_t)((REGb & 0x00ff) | (data << 8)); break;

    case 0x17: mb->result = REG7; break;
    case 0x19: mb->result = REG8; break;
    case 0x18: mb->result = REG9; break;

    case 0x0b:
        REG5 = (int16_t)((REG5 & 0x00ff) | (data << 8));
        REGf = (int16_t)0xffff;
        REG4 = (int16_t)(REG4 - REG2);
        REG5 = (int16_t)(REG5 - REG3);
        goto step_048;

    case 0x11:
        REG5 = (int16_t)((REG5 & 0x00ff) | (data << 8));
        REGf = 0x0000;                       /* do the whole thing in one step */
        goto step_048;

    step_048:
        mb_temp = (int32_t)REG0 * (int32_t)REG4;
        REGc = (int16_t)(mb_temp >> 16);
        REGe = (int16_t)(mb_temp & 0xffff);

        mb_temp = (int32_t)(-REG1) * (int32_t)REG5;
        REG7 = (int16_t)(mb_temp >> 16);
        mb_q = (int16_t)(mb_temp & 0xffff);

        REG7 = (int16_t)(REG7 + REGc);

        REGe = (int16_t)((REGe >> 1) & 0x7fff);      /* rounding */
        REGc = (int16_t)((mb_q >> 1) & 0x7fff);
        mb_q = (int16_t)(REGc + REGe);
        if (mb_q < 0) REG7++;

        mb->result = REG7;
        if (REGf < 0) break;
        REG7 = (int16_t)(REG7 + REG2);
        /* fall through to command 0x12 */

    case 0x12:
        if (opcode == 0x12) { /* entered directly */ }
        mb_temp = (int32_t)REG1 * (int32_t)REG4;
        REGc = (int16_t)(mb_temp >> 16);
        REG9 = (int16_t)(mb_temp & 0xffff);

        mb_temp = (int32_t)REG0 * (int32_t)REG5;
        REG8 = (int16_t)(mb_temp >> 16);
        mb_q = (int16_t)(mb_temp & 0xffff);

        REG8 = (int16_t)(REG8 + REGc);

        REG9 = (int16_t)((REG9 >> 1) & 0x7fff);      /* rounding */
        REGc = (int16_t)((mb_q >> 1) & 0x7fff);
        REG9 = (int16_t)(REG9 + REGc);
        if (REG9 < 0) REG8++;
        REG9 = (int16_t)(REG9 << 1);

        mb->result = REG8;
        if (REGf < 0) break;
        REG8 = (int16_t)(REG8 + REG3);
        REG9 = (int16_t)(REG9 & 0xff00);
        /* fall through to command 0x13 */

    case 0x13:
        if (opcode == 0x13) { /* entered directly */ }
        REGc = REG9;
        mb_q = REG8;
        goto step_0bf;

    case 0x14:
        REGc = REGa;
        mb_q = REGb;
        /* fall through */

    step_0bf:
        REGe = (int16_t)(REG7 ^ mb_q);               /* keep the sign of the result */
        REGd = mb_q;
        if (mb_q >= 0) {
            mb_q = REGc;
        } else {
            REGd = (int16_t)(-mb_q - 1);
            mb_q = (int16_t)(-REGc - 1);
            if ((mb_q < 0) && ((mb_q + 1) < 0)) REGd++;
            mb_q++;
        }

        REGc = (REG7 >= 0) ? REG7 : (int16_t)(-REG7);
        REGf = REG6;                                 /* step counter */
        do {
            REGd = (int16_t)(REGd - REGc);
            msb = ((mb_q & 0x8000) != 0);
            mb_q = (int16_t)((uint16_t)mb_q << 1);
            if (REGd >= 0) mb_q++;
            else           REGd = (int16_t)(REGd + REGc);
            REGd = (int16_t)(REGd << 1);
            REGd = (int16_t)(REGd + msb);
        } while (--REGf >= 0);

        mb->result = (REGe >= 0) ? mb_q : (int16_t)(-mb_q);
        break;

    case 0x1c:                                       /* window test */
        REG5 = (int16_t)((REG5 & 0x00ff) | (data << 8));
        do {
            REGe = (int16_t)((REG4 + REG7) >> 1);
            REGf = (int16_t)((REG5 + REG8) >> 1);
            if ((REGb < REGe) && (REGf < REGe) && ((REGe + REGf) >= 0)) { REG7 = REGe; REG8 = REGf; }
            else                                                        { REG4 = REGe; REG5 = REGf; }
        } while (--REG6 >= 0);
        mb->result = REG8;
        break;

    case 0x1d:
        REG3 = (int16_t)((REG3 & 0x00ff) | (data << 8));
        REG2 = (int16_t)(REG2 - REG0); if (REG2 < 0) REG2 = (int16_t)(-REG2);
        REG3 = (int16_t)(REG3 - REG1); if (REG3 < 0) REG3 = (int16_t)(-REG3);
        /* fall through to command 0x1e */

    case 0x1e:
        /* result = max(REG2, REG3) + 3/8 * min(REG2, REG3): a cheap approximation to hypot */
        if (REG3 >= REG2) { REGc = REG2; REGd = REG3; }
        else              { REGd = REG2; REGc = REG3; }
        REGc = (int16_t)(REGc >> 2);
        REGd = (int16_t)(REGd + REGc);
        REGc = (int16_t)(REGc >> 1);
        mb->result = REGd = (int16_t)(REGc + REGd);
        break;

    default: break;                                  /* 0x1f is a self-test we do not model */
    }
}
