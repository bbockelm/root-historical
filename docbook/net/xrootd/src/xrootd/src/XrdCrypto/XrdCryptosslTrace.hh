// $Id$
#ifndef ___CRYPTOSSL_TRACE_H___
#define ___CRYPTOSSL_TRACE_H___
/******************************************************************************/
/*                                                                            */
/*                 X r d C r y p t o S s l T r a c e . h h                    */
/*                                                                            */
/* (C) 2005 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*                            All Rights Reserved                             */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*               DE-AC03-76-SFO0515 with the Deprtment of Energy              */
/******************************************************************************/

#include <XrdOuc/XrdOucTrace.hh>
#include <XrdCrypto/XrdCryptoAux.hh>

#ifndef NODEBUG

#include "XrdSys/XrdSysHeaders.hh"

#define QTRACE(act) (sslTrace && (sslTrace->What & cryptoTRACE_ ## act))
#define PRINT(y)    {if (sslTrace) {sslTrace->Beg(epname); \
                                       cerr <<y; sslTrace->End();}}
#define TRACE(act,x) if (QTRACE(act)) PRINT(x)
#define DEBUG(y)     TRACE(Debug,y)
#define EPNAME(x)    static const char *epname = x;

#else

#define QTRACE(x)
#define  PRINT(x)
#define  TRACE(x,y)
#define  DEBUG(x)
#define EPNAME(x)

#endif

//
// For error logging and tracing
extern XrdOucTrace *sslTrace;

#endif
