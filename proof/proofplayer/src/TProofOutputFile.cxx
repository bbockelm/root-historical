// @(#)root/proof:$Id$
// Author: Long Tran-Thanh   14/09/07

/*************************************************************************
 * Copyright (C) 1995-2002, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TProofOutputFile                                                     //
//                                                                      //
// Small class to steer the merging of files produced on the workers    //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TProofOutputFile.h"
#include <TEnv.h>
#include <TFileCollection.h>
#include <TFileInfo.h>
#include <TFileMerger.h>
#include <TFile.h>
#include <TList.h>
#include <TObjArray.h>
#include <TObject.h>
#include <TObjString.h>
#include <TProofServ.h>
#include <TSystem.h>
#include <TUUID.h>

ClassImp(TProofOutputFile)

//________________________________________________________________________________
TProofOutputFile::TProofOutputFile(const char *path,
                                   ERunType type, UInt_t opt, const char *dsname)
                 : TNamed(path, ""), fRunType(type), fTypeOpt(opt)
{
   // Main constructor

   fIsLocal = kFALSE;
   fMerged = kFALSE;
   fMerger = 0;
   fDataSet = 0;

   Init(path, dsname);
}

//________________________________________________________________________________
TProofOutputFile::TProofOutputFile(const char *path,
                                   const char *option, const char *dsname)
                 : TNamed(path, "")
{
   // Constructor with the old signature, kept for convenience and backard compatibility.
   // Options:
   //             'M'      merge: finally merge the created files
   //             'L'      local: copy locally the files before merging (implies 'M')
   //             'D'      dataset: create a TFileCollection
   //             'R'      register: dataset run with dataset registration
   //             'O'      overwrite: force dataset replacement during registration
   //             'V'      verify: verify the registered dataset
   // Special 'option' values for backward compatibility:
   //              ""      equivalent to "M"
   //         "LOCAL"      equivalent to "ML" or "L"

   fIsLocal = kFALSE;
   fMerged = kFALSE;
   fMerger = 0;
   fDataSet = 0;

   // Fill the run type and option type
   fRunType = kMerge;
   fTypeOpt = kRemote;
   if (option && strlen(option) > 0) {
      TString opt(option);
      if (opt.Contains("L") || (opt == "LOCAL")) fTypeOpt = kLocal;
      if (!opt.Contains("M") && opt.Contains("D")) {
         // Dataset creation mode
         fRunType = kDataset;
         fTypeOpt = kCreate;
         if (opt.Contains("R")) fTypeOpt = (ETypeOpt) (fTypeOpt | kRegister);
         if (opt.Contains("O")) fTypeOpt = (ETypeOpt) (fTypeOpt | kOverwrite);
         if (opt.Contains("V")) fTypeOpt = (ETypeOpt) (fTypeOpt | kVerify);
      }
   }

   Init(path, dsname);
}

//________________________________________________________________________________
void TProofOutputFile::Init(const char *path, const char *dsname)
{
   // Initializer. Called by all constructors

   fLocalHost = TUrl(gSystem->HostName()).GetHostFQDN();
   Int_t port = gEnv->GetValue("ProofServ.XpdPort", -1);
   if (port > -1) {
      fLocalHost += ":";
      fLocalHost += port;
   }

   TUrl u(path, kTRUE);
   // File name
   fFileName = u.GetFile();
   // The name is used to identify this entity
   SetName(gSystem->BaseName(fFileName.Data()));
   if (dsname && strlen(dsname) > 0) {
      // This is the dataset name in case such option is chosen
      SetTitle(dsname);
   } else {
      // Default dataset name
      SetTitle(GetName());
   }
   // Options and anchor, if any
   if (u.GetOptions() && strlen(u.GetOptions()) > 0)
      fOptionsAnchor += TString::Format("?%s", u.GetOptions());
   if (u.GetAnchor() && strlen(u.GetAnchor()) > 0)
      fOptionsAnchor += TString::Format("#%s", u.GetAnchor());
   // Path
   fIsLocal = kFALSE;
   fDir = u.GetUrl();
   Int_t pos = fDir.Index(fFileName);
   if (pos != kNPOS) fDir.Remove(pos);
   fRawDir = fDir;

   if (fDir == "file:") {
      fIsLocal = kTRUE;
      // For local files, the user is allowed to create files under the assigned directories
      // If this is not the case, the file is rooted automatically to the assigned dir which
      // is the datadir for dataset creation runs, and the working dir for merging runs
      TString dirPath = gSystem->DirName(fFileName);
      TString dirData = (!IsMerge() && gProofServ) ? gProofServ->GetDataDir()
                                                   : gSystem->WorkingDirectory();
      if ((dirPath[0] == '/') && !dirPath.BeginsWith(dirData)) {
         Warning("Init", "not allowed to create files under '%s' - chrooting to '%s'",
                         dirPath.Data(), dirData.Data());
         dirPath.Insert(0, dirData);
      } else if (dirPath.BeginsWith("..")) {
         dirPath.Remove(0, 2);
         if (dirPath[0] != '/') dirPath.Insert(0, "/");
         dirPath.Insert(0, dirData);
      } else if (dirPath[0] == '.' || dirPath[0] == '~') {
         dirPath.Remove(0, 1);
         if (dirPath[0] != '/') dirPath.Insert(0, "/");
         dirPath.Insert(0, dirData);
      } else if (dirPath.IsNull()) {
         dirPath = dirData;
      }
      // Make sure that session-tag, ordinal and query sequential number are present otherwise
      // we may override outputs from other workers
      if (!IsMerge() && gProofServ) {
         if (!dirPath.Contains(gProofServ->GetOrdinal())) {
            if (!dirPath.EndsWith("/")) dirPath += "/";
            dirPath += gProofServ->GetOrdinal();
         }
         if (!dirPath.Contains(gProofServ->GetSessionTag())) {
            if (!dirPath.EndsWith("/")) dirPath += "/";
            dirPath += gProofServ->GetSessionTag();
         }
         if (!dirPath.Contains("<qnum>")) {
            if (!dirPath.EndsWith("/")) dirPath += "/";
            dirPath += "<qnum>";
         }
      }
      // Resolve the relevant placeholders
      TProofServ::ResolveKeywords(dirPath, 0);
      // Save the raw directory
      fRawDir = dirPath;
      // Make sure the the path exists
      // Locate the portion of path already existing and get its mode: we make sure that this
      // mode applies to all new subpaths created
      TString existsPath(dirPath);
      TList subPaths;
      while (existsPath != "/" && existsPath != "." && gSystem->AccessPathName(existsPath)) {
         subPaths.AddFirst(new TObjString(gSystem->BaseName(existsPath)));
         existsPath = gSystem->DirName(existsPath);
      }
      subPaths.SetOwner(kTRUE);
      FileStat_t st;
      if (gSystem->GetPathInfo(existsPath, st) == 0) {
         TString xpath = existsPath;
         TIter nxp(&subPaths);
         TObjString *os = 0;
         while ((os = (TObjString *) nxp())) {
            xpath += TString::Format("/%s", os->GetName());
            if (gSystem->mkdir(xpath, kTRUE) == 0) {
               if (gSystem->Chmod(xpath, (UInt_t) st.fMode) != 0)
                  Warning("Init", "problems setting mode on '%s'", xpath.Data());
            } else {
               Error("Init", "problems creating path '%s'", xpath.Data());
            }
         }
      } else {
         Warning("Init", "could not get info for path '%s': will only try to create"
                         " the full path w/o trying to set the mode", existsPath.Data());
         if (gSystem->mkdir(existsPath, kTRUE) != 0)
            Error("Init", "problems creating path '%s'", existsPath.Data());
      }
      // Remove prefix, if any
      TString pfx  = gEnv->GetValue("Path.Localroot","");
      if (!pfx.IsNull()) dirPath.Remove(0, pfx.Length());
      // Check if a local data server has been specified
      if (gSystem->Getenv("LOCALDATASERVER")) {
         fDir = gSystem->Getenv("LOCALDATASERVER");
         if (!fDir.EndsWith("/")) fDir += "/";
      }
      fDir += dirPath;
   } else {
      // Allow for place holders in fFileName (e.g. root://a.ser.ver//data/dir/<group>/<user>/file)
      TProofServ::ResolveKeywords(fFileName, 0);
   }
   // Notify
   Info("Init", "dir: %s (raw: %s)", fDir.Data(), fRawDir.Data());

   // Default output file name
   fOutputFileName = gEnv->GetValue("Proof.OutputFile", "<file>");
   // Add default file name
   TString fileName = path;
   if (!fileName.EndsWith(".root")) fileName += ".root";
   // Make sure that the file name was inserted (may not happen if the placeholder <file> is missing)
   if (!fOutputFileName.IsNull() && !fOutputFileName.Contains("<file>")) {
      if (!fOutputFileName.EndsWith("/")) fOutputFileName += "/";
         fOutputFileName += fileName;
   }
   // Resolve placeholders
   fileName.ReplaceAll("<ord>",""); // No ordinal in the final merged file
   TProofServ::ResolveKeywords(fOutputFileName, fileName);
   Info("Init", "output file url: %s", fOutputFileName.Data());
   // Fill ordinal
   fWorkerOrdinal = "<ord>";
   TProofServ::ResolveKeywords(fWorkerOrdinal, 0);
}

//________________________________________________________________________________
TProofOutputFile::~TProofOutputFile()
{
   // Main destructor

   if (fDataSet) delete fDataSet;
   if (fMerger) delete fMerger;
}

//______________________________________________________________________________
void TProofOutputFile::SetFileName(const char* name)
{
   // Set the file name

   fFileName = name;
}

//______________________________________________________________________________
void TProofOutputFile::SetOutputFileName(const char *name)
{
   // Set the name of the output file; in the form of an Url.

   if (name && strlen(name) > 0) {
      fOutputFileName = name;
      TProofServ::ResolveKeywords(fOutputFileName);
      Info("SetOutputFileName", "output file url: %s", fOutputFileName.Data());
   } else {
      fOutputFileName = "";
   }
}

//______________________________________________________________________________
TFile* TProofOutputFile::OpenFile(const char* opt)
{
   // Open the file using the unique temporary name

   if (fFileName.IsNull()) return 0;

   // Create the path
   TString fileLoc;
   fileLoc.Form("%s/%s%s", fRawDir.Data(), fFileName.Data(), fOptionsAnchor.Data());

   // Open the file
   TFile *retFile = TFile::Open(fileLoc, opt);

   return retFile;
}

//______________________________________________________________________________
Int_t TProofOutputFile::AdoptFile(TFile *f)
{
   // Adopt a file already open.
   // Return 0 if OK, -1 in case of failure

   if (!f || f->IsZombie())
      return -1;

   // Set the name and dir
   TUrl u(*(f->GetEndpointUrl()));
   fIsLocal = kFALSE;
   if (!strcmp(u.GetProtocol(), "file")) {
      fIsLocal = kTRUE;
      fDir = u.GetFile();
   } else {
      fDir = u.GetUrl();
   }
   fFileName = gSystem->BaseName(fDir.Data());
   fDir.ReplaceAll(fFileName, "");
   fRawDir = fDir;

   // Remove prefix, if any
   TString pfx  = gEnv->GetValue("Path.Localroot","");
   if (!pfx.IsNull()) fDir.ReplaceAll(pfx, "");
   // Include the local data server info, if any
   if (gSystem->Getenv("LOCALDATASERVER")) {
      TString localDS(gSystem->Getenv("LOCALDATASERVER"));
      if (!localDS.EndsWith("/")) localDS += "/";
      fDir.Insert(0, localDS);
   }

   return 0;
}

//______________________________________________________________________________
Long64_t TProofOutputFile::Merge(TCollection* list)
{
   // Merge objects from the list into this object

   // Needs somethign to merge
   if(!list || list->IsEmpty()) return 0;

   if (IsMerge()) {
      // Build-up the merger
      TString fileLoc;
      TString outputFileLoc = (fOutputFileName.IsNull()) ? fFileName : fOutputFileName;
      // Get the file merger instance
      Bool_t localMerge = (fRunType == kMerge && fTypeOpt == kLocal) ? kTRUE : kFALSE;
      TFileMerger *merger = GetFileMerger(localMerge);
      if (!merger) {
         Error("Merge", "could not instantiate the file merger");
         return -1;
      }

      if (!fMerged) {
         merger->OutputFile(outputFileLoc);
         fileLoc.Form("%s/%s", fDir.Data(), GetFileName());
         AddFile(merger, fileLoc);
         fMerged = kTRUE;
      }

      TIter next(list);
      TObject *o = 0;
      while((o = next())) {
         TProofOutputFile *pFile = dynamic_cast<TProofOutputFile *>(o);
         if (pFile) {
            fileLoc = Form("%s/%s", pFile->GetDir(), pFile->GetFileName());
            AddFile(merger, fileLoc);
         }
      }
   } else {
      // Get the reference MSS url, if any
      TUrl mssUrl(gEnv->GetValue("ProofServ.PoolUrl",""));
      // Build-up the TFileCollection
      TFileCollection *dataset = GetFileCollection();
      if (!dataset) {
         Error("Merge", "could not instantiate the file collection");
         return -1;
      }
      TString path;
      TFileInfo *fi = 0;
      // If new, add ourseelves
      dataset->Update();
      if (dataset->GetNFiles() == 0) {
         // Save the export and raw urls
         path.Form("%s/%s%s", GetDir(), GetFileName(), GetOptionsAnchor());
         fi = new TFileInfo(path);
         // Add also an URL with the redirector path, if any
         if (mssUrl.IsValid()) {
            TUrl ur(fi->GetFirstUrl()->GetUrl());
            ur.SetProtocol(mssUrl.GetProtocol());
            ur.SetHost(mssUrl.GetHost());
            ur.SetPort(mssUrl.GetPort());
            if (mssUrl.GetUser() && strlen(mssUrl.GetUser()) > 0)
               ur.SetUser(mssUrl.GetUser());
            fi->AddUrl(ur.GetUrl());
         }
         // Add special local URL to keep track of the file
         path.Form("%s/%s?node=%s", GetDir(kTRUE), GetFileName(), GetLocalHost());
         fi->AddUrl(path);
         fi->Print();
         // Now add to the dataset
         dataset->Add(fi);
      }

      TIter next(list);
      TObject *o = 0;
      while((o = next())) {
         TProofOutputFile *pFile = dynamic_cast<TProofOutputFile *>(o);
         if (pFile) {
            // Save the export and raw urls
            path.Form("%s/%s%s", pFile->GetDir(), pFile->GetFileName(), pFile->GetOptionsAnchor());
            fi = new TFileInfo(path);
            // Add also an URL with the redirector path, if any
            if (mssUrl.IsValid()) {
               TUrl ur(fi->GetFirstUrl()->GetUrl());
               ur.SetProtocol(mssUrl.GetProtocol());
               ur.SetHost(mssUrl.GetHost());
               ur.SetPort(mssUrl.GetPort());
               if (mssUrl.GetUser() && strlen(mssUrl.GetUser()) > 0)
                  ur.SetUser(mssUrl.GetUser());
               fi->AddUrl(ur.GetUrl());
            }
            // Add special local URL to keep track of the file
            path.Form("%s/%s?node=%s", pFile->GetDir(kTRUE), pFile->GetFileName(), pFile->GetLocalHost());
            fi->AddUrl(path);
            fi->Print();
            // Now add to the dataset
            dataset->Add(fi);
         }
      }
   }

   // Done
   return 0;
}

//______________________________________________________________________________
void TProofOutputFile::Print(Option_t *) const
{
   // Dump the class content

   Info("Print","-------------- %s : start (%s) ------------", GetName(), fLocalHost.Data());
   Info("Print"," dir:              %s", fDir.Data());
   Info("Print"," raw dir:          %s", fRawDir.Data());
   Info("Print"," file name:        %s%s", fFileName.Data(), fOptionsAnchor.Data());
   if (IsMerge()) {
      Info("Print"," run type:         create a merged file");
      Info("Print"," merging option:   %s",
                       (fTypeOpt == kLocal) ? "local copy" : "keep remote");
   } else {
      TString opt;
      if ((fTypeOpt & kRegister)) opt += "R";
      if ((fTypeOpt & kOverwrite)) opt += "O";
      if ((fTypeOpt & kVerify)) opt += "V";
      Info("Print"," run type:         create dataset (name: '%s', opt: '%s')",
                                         GetTitle(), opt.Data());
   }
   Info("Print"," output file name: %s", fOutputFileName.Data());
   Info("Print"," ordinal:          %s", fWorkerOrdinal.Data());
   Info("Print","-------------- %s : done -------------", GetName());

   return;
}

//______________________________________________________________________________
void TProofOutputFile::NotifyError(const char *msg)
{
   // Notify error message

   if (msg) {
      if (gProofServ)
         gProofServ->SendAsynMessage(msg);
      else
         Printf("%s", msg);
   } else {
      Info("NotifyError","called with empty message");
   }

   return;
}

//______________________________________________________________________________
void TProofOutputFile::AddFile(TFileMerger *merger, const char *path)
{
   // Add file to merger, checking the result

   if (merger && path) {
      if (!merger->AddFile(path))
         NotifyError(Form("TProofOutputFile::AddFile:"
                          " error from TFileMerger::AddFile(%s)", path));
   }
}

//______________________________________________________________________________
void TProofOutputFile::Unlink(const char *path)
{
   // Unlink path

   if (path) {
      if (!gSystem->AccessPathName(path)) {
         if (gSystem->Unlink(path) != 0)
            NotifyError(Form("TProofOutputFile::Unlink:"
                             " error from TSystem::Unlink(%s)", path));
      }
   }
}

//______________________________________________________________________________
TFileCollection *TProofOutputFile::GetFileCollection()
{
   // Get instance of the file collection to be used in 'dataset' mode

   if (!fDataSet)
      fDataSet = new TFileCollection(GetTitle());
   return fDataSet;
}

//______________________________________________________________________________
TFileMerger *TProofOutputFile::GetFileMerger(Bool_t local)
{
   // Get instance of the file merger to be used in 'merge' mode

   if (!fMerger)
      fMerger = new TFileMerger(local);
   return fMerger;
}
