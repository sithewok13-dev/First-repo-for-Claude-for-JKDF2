// Added: entire file. See jkNetExt.h.

#include "Dss/jkNetExt.h"

#ifdef QOL_IMPROVEMENTS

#include <string.h>
#include "jk.h"
#include "Dss/sithMulti.h"
#include "Devices/sithComm.h"
#include "Platform/Networking/stdCommTransport.h"

static jkNetExtHandler_t jkNetExt_apfHandler[JKNETEXT_NUM_KINDS];

static int jkNetExt_Process(SithMessage* pMsg)
{
    if (!pMsg || pMsg->netMsg.msg_size < 3)
        return 1;
    const uint8_t* p = (const uint8_t*)&pMsg->pktData[0];
    const int kind = p[0];
    const int len = p[1] | (p[2] << 8);
    if (len > (int)pMsg->netMsg.msg_size - 3 || len > JKNETEXT_MAX_LEN)
        return 1;
    if (kind > 0 && kind < JKNETEXT_NUM_KINDS && jkNetExt_apfHandler[kind])
        jkNetExt_apfHandler[kind](pMsg->netMsg.idx, p + 3, len);
    return 1;
}

void jkNetExt_Startup(void)
{
    sithMessage_RegisterFunction(DSS_JKNETEXT, jkNetExt_Process);
}

void jkNetExt_Register(int kind, jkNetExtHandler_t pfHandler)
{
    if (kind > 0 && kind < JKNETEXT_NUM_KINDS)
        jkNetExt_apfHandler[kind] = pfHandler;
}

void jkNetExt_Send(int kind, const void* pData, int len, int idTo)
{
    if (!sithNet_isMulti || kind <= 0 || kind >= JKNETEXT_NUM_KINDS || len < 0 || len > JKNETEXT_MAX_LEN)
        return;
#ifdef STDCOMM_TRANSPORTS
    // (a classic host's game, over GNS: its host and players are other builds, which know none of these)
    if (stdCommTransport_IsClassic())
        return;
#endif
    uint8_t* p = (uint8_t*)&sithComm_netMsgTmp.pktData[0];
    p[0] = (uint8_t)kind;
    p[1] = (uint8_t)(len & 0xFF);
    p[2] = (uint8_t)(len >> 8);
    if (len)
        memcpy(p + 3, pData, len);
    sithComm_netMsgTmp.netMsg.flag_maybe = 0;
    sithComm_netMsgTmp.netMsg.cogMsgId = DSS_JKNETEXT;
    sithComm_netMsgTmp.netMsg.msg_size = 3 + len;
    sithComm_SendMsgToPlayer(&sithComm_netMsgTmp, idTo, 1, 1);
}

#endif // QOL_IMPROVEMENTS
