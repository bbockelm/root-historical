// @(#)root/pgsql:$Id$
// Author: Dennis Box (dbox@fnal.gov)  3/12/2007

/*************************************************************************
 * Copyright (C) 1995-2007, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
//  SQL statement class for PgSQL                                       //
//                                                                      //
//  See TSQLStatement class documentation for more details.             //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TPgSQLStatement.h"
#include "TDataType.h"
#include "TDatime.h"

#include <stdlib.h>

ClassImp(TPgSQLStatement)

#ifdef PG_VERSION_NUM

//______________________________________________________________________________
TPgSQLStatement::TPgSQLStatement(PgSQL_Stmt_t* stmt, Bool_t errout):
   TSQLStatement(errout),
   fStmt(stmt),
   fNumBuffers(0),
   fBind(0),
   fFieldName(0),
   fWorkingMode(0),
   fIterationCount(0),
   fParamLengths(0),
   fParamFormats(0),
   fNumResultRows(0),
   fNumResultCols(0)
{
   // Normal constructor.
   // Checks if statement contains parameters tags.

   fStmt->fRes = PQdescribePrepared(fStmt->fConn,"");
   unsigned long paramcount = PQnparams(fStmt->fRes);
   fNumResultCols = PQnfields(fStmt->fRes);
   fIterationCount = -1;

   if (paramcount>0) {
      fWorkingMode = 1;
      SetBuffersNumber(paramcount);
   } else {
      fWorkingMode = 2;
      SetBuffersNumber(fNumResultCols);
   }
}

//______________________________________________________________________________
TPgSQLStatement::~TPgSQLStatement()
{
   // Destructor.

   Close();
}

//______________________________________________________________________________
void TPgSQLStatement::Close(Option_t *)
{
   // Close statement.

   if (fStmt->fRes)
      PQclear(fStmt->fRes);

   fStmt->fRes = 0;

   FreeBuffers();
   //TPgSQLServers responsibility to free connection
   fStmt->fConn=0;
   delete fStmt;
}


// Reset error and check that statement exists
#define CheckStmt(method, res)                          \
   {                                                    \
      ClearError();                                     \
      if (fStmt==0) {                                   \
         SetError(-1,"Statement handle is 0",method);   \
         return res;                                    \
      }                                                 \
   }

#define CheckErrNo(method, force, wtf)                  \
   {                                                    \
      int stmterrno = PQresultStatus(fStmt->fRes);      \
      if ((stmterrno!=0) || force) {                        \
         const char* stmterrmsg = PQresultErrorMessage(fStmt->fRes);  \
         if (stmterrno==0) { stmterrno = -1; stmterrmsg = "PgSQL statement error"; } \
         SetError(stmterrno, stmterrmsg, method);               \
         return wtf;                                    \
      }                                                 \
   }

// check last pgsql statement error code
#define CheckGetField(method, res)                      \
   {                                                    \
      ClearError();                                     \
      if (!IsResultSetMode()) {                         \
         SetError(-1,"Cannot get statement parameters",method); \
         return res;                                    \
      }                                                 \
      if ((npar<0) || (npar>=fNumBuffers)) {            \
         SetError(-1,Form("Invalid parameter number %d", npar),method); \
         return res;                                    \
      }                                                 \
   }

//________________________________________________________________________
Bool_t TPgSQLStatement::Process()
{
   // Process statement.

   CheckStmt("Process",kFALSE);

   if (IsSetParsMode()) {
      fStmt->fRes= PQexecPrepared(fStmt->fConn,"",fNumBuffers,
                                 (const char* const*)fBind,
                                 0,0,0);

   } else { //result set mode

      fStmt->fRes= PQexecPrepared(fStmt->fConn,"",0,(const char* const*) 0,0,0,0);
   }
   ExecStatusType stat = PQresultStatus(fStmt->fRes);
   if (!pgsql_success(stat))
      CheckErrNo("Process",kTRUE, kFALSE);
   return kTRUE;
}

//________________________________________________________________________
Int_t TPgSQLStatement::GetNumAffectedRows()
{
   // Return number of affected rows after statement is processed.

   CheckStmt("GetNumAffectedRows", -1);

   return (Int_t) atoi(PQcmdTuples(fStmt->fRes));
}

//______________________________________________________________________________
Int_t TPgSQLStatement::GetNumParameters()
{
   // Return number of statement parameters.

   CheckStmt("GetNumParameters", -1);

   Int_t res = PQnparams(fStmt->fRes);

   CheckErrNo("GetNumParameters", kFALSE, -1);

   return res;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::StoreResult()
{
   // Store result of statement processing to access them
   // via GetInt(), GetDouble() and so on methods.

   int i;
   for (i=0;i<fNumResultCols;i++){
      fFieldName[i] = PQfname(fStmt->fRes,i);
      fParamFormats[i]=PQftype(fStmt->fRes,i);
      fParamLengths[i]=PQfsize(fStmt->fRes,i);

   }
   fNumResultRows=PQntuples(fStmt->fRes);
   ExecStatusType stat = PQresultStatus(fStmt->fRes);
   fWorkingMode = 2;
   if (!pgsql_success(stat))
      CheckErrNo("StoreResult",kTRUE, kFALSE);
   return kTRUE;
}

//______________________________________________________________________________
Int_t TPgSQLStatement::GetNumFields()
{
   // Return number of fields in result set.

   if (fWorkingMode==1)
      return fNumBuffers;
   if (fWorkingMode==2)
      return fNumResultCols;
   return -1;
}

//______________________________________________________________________________
const char* TPgSQLStatement::GetFieldName(Int_t nfield)
{
   // Returns field name in result set.

   if (!IsResultSetMode() || (nfield<0) || (nfield>=fNumBuffers)) return 0;

   return fFieldName[nfield];
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::NextResultRow()
{
   // Shift cursor to nect row in result set.

   if ((fStmt==0) || !IsResultSetMode()) return kFALSE;

   Bool_t res=kTRUE;

   fIterationCount++;
   if (fIterationCount>=fNumResultRows)
     res=kFALSE;
   return res;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::NextIteration()
{
   // Increment iteration counter for statement, where parameter can be set.
   // Statement with parameters of previous iteration
   // automatically will be applied to database.

   ClearError();

   if (!IsSetParsMode() || (fBind==0)) {
      SetError(-1,"Cannot call for that statement","NextIteration");
      return kFALSE;
   }

   fIterationCount++;

   if (fIterationCount==0) return kTRUE;

   fStmt->fRes= PQexecPrepared(fStmt->fConn,"",fNumBuffers,
                               (const char* const*)fBind,
                               0,//fParamLengths,
                               0,//fParamFormats,
                               0);
   ExecStatusType stat = PQresultStatus(fStmt->fRes);
   if (!pgsql_success(stat) ){
      CheckErrNo("NextIteration", kTRUE, kFALSE) ;
      return kFALSE;
   }
   return kTRUE;
}

//______________________________________________________________________________
void TPgSQLStatement::FreeBuffers()
{
   // Release all buffers, used by statement.

  //individual field names free()'ed by PQClear of fStmt->fRes
   if (fFieldName)
      delete[] fFieldName;

   if (fBind){
      for (Int_t i=0;i<fNumBuffers;i++)
         delete [] fBind[i];
      delete[] fBind;
   }

   if (fParamLengths)
      delete [] fParamLengths;

   if (fParamFormats)
      delete [] fParamFormats;

   fFieldName = 0;
   fBind = 0;
   fNumBuffers = 0;
   fParamLengths = 0;
   fParamFormats = 0;
}

//______________________________________________________________________________
void TPgSQLStatement::SetBuffersNumber(Int_t numpars)
{
   // Allocate buffers for statement parameters/ result fields.

   FreeBuffers();
   if (numpars<=0) return;

   fNumBuffers = numpars;

   fBind = new char*[fNumBuffers];
   for(int i=0; i<fNumBuffers; ++i){
      fBind[i]=new char[25]; //big enough to handle text rep. of 64 bit number
   }
   fFieldName = new char*[fNumBuffers];

   fParamLengths = new int[fNumBuffers];
   memset(fParamLengths, 0, sizeof(int)*fNumBuffers);

   fParamFormats = new int[fNumBuffers];
   memset(fParamFormats, 0, sizeof(int)*fNumBuffers);
}

//______________________________________________________________________________
const char* TPgSQLStatement::ConvertToString(Int_t npar)
{
   // Convert field value to string.

   const char *buf = PQgetvalue(fStmt->fRes, fIterationCount, npar);
   return buf;
}

//______________________________________________________________________________
long double TPgSQLStatement::ConvertToNumeric(Int_t npar)
{
   // Convert field to numeric.

   if (PQgetisnull(fStmt->fRes,fIterationCount,npar))
      return (long double)0;

   return (long double) atof(PQgetvalue(fStmt->fRes,fIterationCount,npar));
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::IsNull(Int_t npar)
{
   // Checks if field value is null.

   CheckGetField("IsNull", kTRUE);

   return PQgetisnull(fStmt->fRes,fIterationCount,npar);
}

//______________________________________________________________________________
Int_t TPgSQLStatement::GetInt(Int_t npar)
{
   // Get integer.

   if (PQgetisnull(fStmt->fRes,fIterationCount,npar))
      return (Int_t)0;

   return (Int_t) atoi(PQgetvalue(fStmt->fRes,fIterationCount,npar));
}

//______________________________________________________________________________
UInt_t TPgSQLStatement::GetUInt(Int_t npar)
{
   // Get unsigned integer.

   if (PQgetisnull(fStmt->fRes,fIterationCount,npar))
      return (UInt_t)0;

   return (UInt_t) atoi(PQgetvalue(fStmt->fRes,fIterationCount,npar));
}

//______________________________________________________________________________
Long_t TPgSQLStatement::GetLong(Int_t npar)
{
   // Get long.

   if (PQgetisnull(fStmt->fRes,fIterationCount,npar))
      return (Long_t)0;

   return (Long_t) atol(PQgetvalue(fStmt->fRes,fIterationCount,npar));
}

//______________________________________________________________________________
Long64_t TPgSQLStatement::GetLong64(Int_t npar)
{
   // Get long64.

   if (PQgetisnull(fStmt->fRes,fIterationCount,npar))
      return (Long64_t)0;

#ifndef R__WIN32
   return (Long64_t) atoll(PQgetvalue(fStmt->fRes,fIterationCount,npar));
#else
   return (Long64_t) _atoi64(PQgetvalue(fStmt->fRes,fIterationCount,npar));
#endif
}

//______________________________________________________________________________
ULong64_t TPgSQLStatement::GetULong64(Int_t npar)
{
   // Return field value as unsigned 64-bit integer

   if (PQgetisnull(fStmt->fRes,fIterationCount,npar))
      return (ULong64_t)0;

#ifndef R__WIN32
   return (ULong64_t) atoll(PQgetvalue(fStmt->fRes,fIterationCount,npar));
#else
   return (ULong64_t) _atoi64(PQgetvalue(fStmt->fRes,fIterationCount,npar));
#endif
}

//______________________________________________________________________________
Double_t TPgSQLStatement::GetDouble(Int_t npar)
{
   // Return field value as double.

   if (PQgetisnull(fStmt->fRes,fIterationCount,npar))
      return (Double_t)0;
   return (Double_t) atof(PQgetvalue(fStmt->fRes,fIterationCount,npar));
}

//______________________________________________________________________________
const char *TPgSQLStatement::GetString(Int_t npar)
{
   // Return field value as string.

   return PQgetvalue(fStmt->fRes,fIterationCount,npar);
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::GetBinary(Int_t npar, void* &mem, Long_t& size)
{
   // Return field value as binary array.
   // Note PQgetvalue mallocs/frees and ROOT classes expect new/delete.

   size_t sz;
   char *cptr = PQgetvalue(fStmt->fRes,fIterationCount,npar);
   unsigned char * mptr = PQunescapeBytea((const unsigned char*)cptr,&sz);
   if ((Long_t)sz>size) {
      delete [] (unsigned char*) mem;
      mem = (void*) new unsigned char[sz];
   }
   size=sz;
   memcpy(mem,mptr,sz);
   PQfreemem(mptr);
   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::GetDate(Int_t npar, Int_t& year, Int_t& month, Int_t& day)
{
   // Return field value as date.

   TString val=PQgetvalue(fStmt->fRes,fIterationCount,npar);
   TDatime d = TDatime(val.Data());
   year = d.GetYear();
   month = d.GetMonth();
   day= d.GetDay();
   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::GetTime(Int_t npar, Int_t& hour, Int_t& min, Int_t& sec)
{
   // Return field as time.

   TString val=PQgetvalue(fStmt->fRes,fIterationCount,npar);
   TDatime d = TDatime(val.Data());
   hour = d.GetHour();
   min = d.GetMinute();
   sec= d.GetSecond();
   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::GetDatime(Int_t npar, Int_t& year, Int_t& month, Int_t& day, Int_t& hour, Int_t& min, Int_t& sec)
{
   // Return field value as date & time.

   TString val=PQgetvalue(fStmt->fRes,fIterationCount,npar);
   TDatime d = TDatime(val.Data());
   year = d.GetYear();
   month = d.GetMonth();
   day= d.GetDay();
   hour = d.GetHour();
   min = d.GetMinute();
   sec= d.GetSecond();
   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::GetTimestamp(Int_t npar, Int_t& year, Int_t& month, Int_t& day, Int_t& hour, Int_t& min, Int_t& sec, Int_t& frac)
{
   // Return field as timestamp.

   TString val=PQgetvalue(fStmt->fRes,fIterationCount,npar);
   Ssiz_t p = val.Last('.');
   TSubString s_frac = val(p,val.Length()-p+1);
   TDatime d = TDatime(val.Data());
   year = d.GetYear();
   month = d.GetMonth();
   day= d.GetDay();
   hour = d.GetHour();
   min = d.GetMinute();
   sec= d.GetSecond();
   frac=atoi(s_frac.Data());
   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetNull(Int_t npar)
{
   // Set NULL as parameter value.
   // If NULL should be set for statement parameter during first iteration,
   // one should call before proper Set... method to identify type of argument for
   // the future. For instance, if one suppose to have double as type of parameter,
   // code should look like:
   //    stmt->SetDouble(2, 0.);
   //    stmt->SetNull(2);

   fBind[npar][0] = 0;

   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetInt(Int_t npar, Int_t value)
{
   // Set parameter value as integer.

   sprintf(fBind[npar],"%d",value);

   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetUInt(Int_t npar, UInt_t value)
{
   // Set parameter value as unsinged integer.

   sprintf(fBind[npar],"%u",value);

   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetLong(Int_t npar, Long_t value)
{
   // Set parameter value as long.

   sprintf(fBind[npar],"%ld",value);

   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetLong64(Int_t npar, Long64_t value)
{
   // Set parameter value as 64-bit integer.

   sprintf(fBind[npar],"%lld",(Long64_t)value);

   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetULong64(Int_t npar, ULong64_t value)
{
   // Set parameter value as unsinged 64-bit integer.

   sprintf(fBind[npar],"%llu",(ULong64_t)value);

   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetDouble(Int_t npar, Double_t value)
{
   // Set parameter value as double value.

   sprintf(fBind[npar],"%lf",value);

   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetString(Int_t npar, const char* value, Int_t maxsize)
{
   // Set parameter value as string.

   if(sizeof(fBind[npar])<(unsigned)maxsize){
      delete [] fBind[npar];
      fBind[npar] = new char[maxsize];
   }
   strlcpy(fBind[npar],value,maxsize);
   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetBinary(Int_t npar, void* mem, Long_t size, Long_t maxsize)
{
   // Set parameter value as binary data.

   size_t sz=size;
   size_t mxsz=maxsize;
   char* mptr = (char*)malloc(2*sz+1);
   mxsz=PQescapeString (mptr,(char*)mem,sz);

   delete [] fBind[npar];
   fBind[npar]= new char[mxsz+1];
   memcpy(fBind[npar],mptr,mxsz);
   free(mptr);
   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetDate(Int_t npar, Int_t year, Int_t month, Int_t day)
{
   // Set parameter value as date.

   TDatime d =TDatime(year,month,day,0,0,0);
   sprintf(fBind[npar],"%s",(char*)d.AsSQLString());

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetTime(Int_t npar, Int_t hour, Int_t min, Int_t sec)
{
   // Set parameter value as time.

   TDatime d=TDatime(2000,1,1,hour,min,sec);
   sprintf(fBind[npar],"%s",(char*)d.AsSQLString());
   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetDatime(Int_t npar, Int_t year, Int_t month, Int_t day, Int_t hour, Int_t min, Int_t sec)
{
   // Set parameter value as date & time.

   TDatime d=TDatime(year,month,day,hour,min,sec);
   sprintf(fBind[npar],"%s",(char*)d.AsSQLString());
   return kTRUE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetTimestamp(Int_t npar, Int_t year, Int_t month, Int_t day, Int_t hour, Int_t min, Int_t sec, Int_t)
{
   // Set parameter value as timestamp.

   TDatime d(year,month,day,hour,min,sec);
   sprintf(fBind[npar],"%s",(char*)d.AsSQLString());
   return kTRUE;
}

#else

//______________________________________________________________________________
TPgSQLStatement::TPgSQLStatement(PgSQL_Stmt_t*, Bool_t)
{
   // Normal constructor.
   // For PgSQL version < 8.2 no statement is supported.
}

//______________________________________________________________________________
TPgSQLStatement::~TPgSQLStatement()
{
   // Destructor.
}

//______________________________________________________________________________
void TPgSQLStatement::Close(Option_t *)
{
   // Close statement.
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::Process()
{
   // Process statement.

   return kFALSE;
}

//______________________________________________________________________________
Int_t TPgSQLStatement::GetNumAffectedRows()
{
   // Return number of affected rows after statement is processed.

   return 0;
}

//______________________________________________________________________________
Int_t TPgSQLStatement::GetNumParameters()
{
   // Return number of statement parameters.

   return 0;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::StoreResult()
{
   // Store result of statement processing to access them
   // via GetInt(), GetDouble() and so on methods.

   return kFALSE;
}

//______________________________________________________________________________
Int_t TPgSQLStatement::GetNumFields()
{
   // Return number of fields in result set.

   return 0;
}

//______________________________________________________________________________
const char* TPgSQLStatement::GetFieldName(Int_t)
{
   // Returns field name in result set.

   return 0;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::NextResultRow()
{
   // Shift cursor to nect row in result set.

   return kFALSE;
}


//______________________________________________________________________________
Bool_t TPgSQLStatement::NextIteration()
{
   // Increment iteration counter for statement, where parameter can be set.
   // Statement with parameters of previous iteration
   // automatically will be applied to database.

   return kFALSE;
}

//______________________________________________________________________________
void TPgSQLStatement::FreeBuffers()
{
   // Release all buffers, used by statement.
}

//______________________________________________________________________________
void TPgSQLStatement::SetBuffersNumber(Int_t)
{
   // Allocate buffers for statement parameters/ result fields.
}

//______________________________________________________________________________
const char* TPgSQLStatement::ConvertToString(Int_t)
{
   // Convert field value to string.

   return 0;
}

//______________________________________________________________________________
long double TPgSQLStatement::ConvertToNumeric(Int_t)
{
   // Convert field to numeric value.

   return 0;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::IsNull(Int_t)
{
   // Checks if field value is null.

   return kTRUE;
}

//______________________________________________________________________________
Int_t TPgSQLStatement::GetInt(Int_t)
{
   // Return field value as integer.

   return 0;
}

//______________________________________________________________________________
UInt_t TPgSQLStatement::GetUInt(Int_t)
{
   // Return field value as unsigned integer.

   return 0;
}

//______________________________________________________________________________
Long_t TPgSQLStatement::GetLong(Int_t)
{
   // Return field value as long integer.

   return 0;
}

//______________________________________________________________________________
Long64_t TPgSQLStatement::GetLong64(Int_t)
{
   // Return field value as 64-bit integer.

   return 0;
}

//______________________________________________________________________________
ULong64_t TPgSQLStatement::GetULong64(Int_t)
{
   // Return field value as unsigned 64-bit integer.

   return 0;
}

//______________________________________________________________________________
Double_t TPgSQLStatement::GetDouble(Int_t)
{
   // Return field value as double.

   return 0.;
}

//______________________________________________________________________________
const char *TPgSQLStatement::GetString(Int_t)
{
   // Return field value as string.

   return 0;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::GetBinary(Int_t, void* &, Long_t&)
{
   // Return field value as binary array.

   return kFALSE;
}


//______________________________________________________________________________
Bool_t TPgSQLStatement::GetDate(Int_t, Int_t&, Int_t&, Int_t&)
{
   // Return field value as date.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::GetTime(Int_t, Int_t&, Int_t&, Int_t&)
{
   // Return field value as time.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::GetDatime(Int_t, Int_t&, Int_t&, Int_t&, Int_t&, Int_t&, Int_t&)
{
   // Return field value as date & time.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::GetTimestamp(Int_t, Int_t&, Int_t&, Int_t&, Int_t&, Int_t&, Int_t&, Int_t&)
{
   // Return field value as time stamp.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetSQLParamType(Int_t, int, bool, int)
{
   // Set parameter type to be used as buffer.
   // Used in both setting data to database and retriving data from data base.
   // Initialize proper PGSQL_BIND structure and allocate required buffers.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetNull(Int_t)
{
   // Set NULL as parameter value.
   // If NULL should be set for statement parameter during first iteration,
   // one should call before proper Set... method to identify type of argument for
   // the future. For instance, if one suppose to have double as type of parameter,
   // code should look like:
   //    stmt->SetDouble(2, 0.);
   //    stmt->SetNull(2);

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetInt(Int_t, Int_t)
{
   // Set parameter value as integer.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetUInt(Int_t, UInt_t)
{
   // Set parameter value as unsigned integer.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetLong(Int_t, Long_t)
{
   // Set parameter value as long integer.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetLong64(Int_t, Long64_t)
{
   // Set parameter value as 64-bit integer.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetULong64(Int_t, ULong64_t)
{
   // Set parameter value as unsigned 64-bit integer.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetDouble(Int_t, Double_t)
{
   // Set parameter value as double.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetString(Int_t, const char*, Int_t)
{
   // Set parameter value as string.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetBinary(Int_t, void*, Long_t, Long_t)
{
   // Set parameter value as binary data.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetDate(Int_t, Int_t, Int_t, Int_t)
{
   // Set parameter value as date.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetTime(Int_t, Int_t, Int_t, Int_t)
{
   // Set parameter value as time.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetDatime(Int_t, Int_t, Int_t, Int_t, Int_t, Int_t, Int_t)
{
   // Set parameter value as date & time.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TPgSQLStatement::SetTimestamp(Int_t, Int_t, Int_t, Int_t, Int_t, Int_t, Int_t, Int_t)
{
   // Set parameter value as timestamp.

   return kFALSE;
}

#endif  //PG_VERSION_NUM
