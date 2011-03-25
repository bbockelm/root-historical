#ifndef __FRMMIGRATE__
#define __FRMMIGRATE__
/******************************************************************************/
/*                                                                            */
/*                      X r d F r m M i g r a t e . h h                       */
/*                                                                            */
/* (c) 2010 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*                            All Rights Reserved                             */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*              DE-AC02-76-SFO0515 with the Department of Energy              */
/******************************************************************************/

#include <time.h>
#include <sys/types.h>

class XrdFrmFileset;
class XrdFrmXfrQueue;
class XrdOucTList;

class XrdFrmMigrate
{
public:

static void          Display();

static void          Queue(XrdFrmFileset *sP);

static void          Migrate(int doinit=1);

                     XrdFrmMigrate() {}
                    ~XrdFrmMigrate() {}

private:

// Methods
//
static void          Add(XrdFrmFileset *fsp);
static int           Advance();
static void          Defer(XrdFrmFileset *sP);
static const char   *Eligible(XrdFrmFileset *sP, time_t &xTime);
static void          Scan();

// Static Variables

static XrdFrmFileset   *fsDefer;
static int              numMig;
};
#endif
