// @(#)root/proof:$Id$
// Author:

/*************************************************************************
 * Copyright (C) 1995-2005, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TProofNode                                                           //
//                                                                      //
// PROOF worker node information                                        //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TProofNodes.h"
#include "TProof.h"
#include "TList.h"
#include "TMap.h"
#include "TObjString.h"

ClassImp(TProofNodes)

//______________________________________________________________________________
TProofNodes::TProofNodes(TProof* proof)
            : fProof(proof), fNodes(0), fActiveNodes(0),
              fMaxWrksNode(-1), fMinWrksNode(-1),
              fNNodes(0), fNWrks(0), fNActiveWrks(0)  
{
   // Constructor

   Build();
}

//______________________________________________________________________________
TProofNodes::~TProofNodes()
{
   // Destructor

   if (fNodes) {
      fNodes->SetOwner(kTRUE);
      SafeDelete(fNodes);
   }
}

//______________________________________________________________________________
void TProofNodes::Build()
{
   // Desctiption: Build the node list, which is a list of nodes whose members
   //             in turn are lists of workers on the node.
   // Input: Nothing
   // Return: Nothing

   if (!fProof || !fProof->IsValid()) {
      Warning("Build", "the PROOF instance is undefined or invalid! Cannot continue"); 
      return;
   }

   if (fNodes){
      fNodes->SetOwner(kTRUE);
      SafeDelete(fNodes);
   }
   fNodes = new TMap;
   fNodes->SetOwner(kTRUE);

   TList *slaves = fProof->GetListOfSlaveInfos();
   TIter nxtslave(slaves);
   TSlaveInfo *si = 0;
   TList *node = 0;
   TPair *pair = 0;
   while ((si = (TSlaveInfo *)(nxtslave()))) {
      TSlaveInfo *si_copy = (TSlaveInfo *)(si->Clone());
      if (!(pair = (TPair *) fNodes->FindObject(si->GetName()))) {
         node = new TList;
         //si's are owned by the member fSlaveInfo of fProof
         node->SetOwner(kTRUE); 
         node->SetName(si_copy->GetName());
         node->Add(si_copy);
         fNodes->Add(new TObjString(si->GetName()), node);
      } else {
         node = (TList *) pair->Value();
         node->Add(si_copy);
      }
   }
   // Update counters and created active nodes map
   if (fActiveNodes){
      fActiveNodes->SetOwner(kTRUE);
      SafeDelete(fActiveNodes);
   }
   fActiveNodes = new TMap;
   fActiveNodes->SetOwner(kTRUE);
   TList *actnode = 0;
   fMaxWrksNode = -1;
   fMinWrksNode = -1;
   fNNodes = 0;
   fNWrks = 0;
   fNActiveWrks = 0;
   TIter nxk(fNodes);
   TObject *key = 0;
   while ((key = nxk()) != 0) {
      node = dynamic_cast<TList *>(fNodes->GetValue(key));
      if (node) {
         fNNodes++;
         fNWrks += node->GetSize();
         if (fMinWrksNode == -1 || (node->GetSize() < fMinWrksNode)) {
            fMinWrksNode = node->GetSize();
         }
         if (fMaxWrksNode == -1 || (node->GetSize() > fMaxWrksNode)) {
            fMaxWrksNode = node->GetSize();
         }
         TIter nxw(node);
         while ((si = (TSlaveInfo *) nxw())) {
            if (si->fStatus == TSlaveInfo::kActive) {
               fNActiveWrks++;
               TSlaveInfo *si_copy = (TSlaveInfo *)(si->Clone());
               actnode = dynamic_cast<TList *>(fActiveNodes->GetValue(key));
               if (actnode) {
                  actnode->Add(si_copy);
               } else {
                  actnode = new TList;
                  actnode->SetOwner(kTRUE); 
                  actnode->SetName(si_copy->GetName());
                  actnode->Add(si_copy);
                  fActiveNodes->Add(new TObjString(si->GetName()), actnode);
               }
            }
         }
      } else {
         Warning("Build", "could not get list for node '%s'", key->GetName()); 
      }
   }

   // Done
   return;
}

//______________________________________________________________________________
Int_t TProofNodes::ActivateWorkers(Int_t nwrks)
{
   // Description: Activate 'nwrks' workers; calls TProof::SetParallel and
   //              rebuild the internal lists
   // Input: number of workers
   // Return: 0 is successful, <0 otherwise.

   Int_t nw = fProof->SetParallel(nwrks);
   if (nw > 0) {
      if (nw != nwrks)
         Warning("ActivateWorkers", "requested %d got %d", nwrks, nw);
      Build();
   }
   return nw;
}

//______________________________________________________________________________
Int_t TProofNodes::ActivateWorkers(const char *workers)
{
   // Description: Activate the same number of workers on all nodes.
   // Input: workers: string of the form "nx" where non-negative integer n
   //                 is the number of worker on each node to be activated.
   // Return: The number of active workers per node when the operation is
   //         successful.
   //         <0 otherwise.

   //Make sure worker list is up-to-date
   Build();

   TString sworkers = TString(workers).Strip(TString::kTrailing, 'x');
   if (!sworkers.IsDigit()) {
      Error("ActivateWorkers", "wrongly formatted argument: %s - cannot continue", workers);
      return -1;
   }
   Int_t nworkersnode = sworkers.Atoi();
   Int_t ret = nworkersnode;
   TSlaveInfo *si = 0;
   TIter nxtnode(fNodes);
   TList *node = 0;
   TIter *nxk = new TIter(fNodes);
   TObject *key = 0;
   while ((key = nxk->Next()) != 0) {
      if ((node = dynamic_cast<TList *>(fNodes->GetValue(key)))) {
         TIter nxtworker(node);
         Int_t nactiveworkers = 0;
         while ((si = (TSlaveInfo *)(nxtworker()))) {
            const char *ordinal = si->GetOrdinal();
            if (nactiveworkers < nworkersnode) {
               if (si->fStatus == TSlaveInfo::kNotActive) {
                  fProof->ActivateWorker(ordinal);
               }
               nactiveworkers++;
            } else {
               if (si->fStatus == TSlaveInfo::kActive) {
                  fProof->DeactivateWorker(ordinal);
               }
            }
         }
      } else {
         Warning("ActivateWorkers", "could not get list for node '%s'", key->GetName()); 
      }
   }

   // Rebuild
   Build();

   SafeDelete(nxk);
   nxk = new TIter(fNodes);
   while ((key = nxk->Next()) != 0) {
      if ((node = dynamic_cast<TList *>(fNodes->GetValue(key)))) {
         TIter nxtworker(node);
         Int_t nactiveworkers = 0;
         while ((si = (TSlaveInfo *)(nxtworker()))) {
            if (si->fStatus == TSlaveInfo::kActive) nactiveworkers++;
         }
         if (nactiveworkers != nworkersnode) {
            Warning("ActivateWorkers", "only %d (out of %d requested) workers "
                                       "were activated on node %s",
                                       nactiveworkers, nworkersnode, node->GetName());
            ret = -1;
         }
      } else {
         Warning("ActivateWorkers", "could not get list for node '%s'", key->GetName()); 
      }
   }
   SafeDelete(nxk);

   // Done
   return ret;
}

//______________________________________________________________________________
void TProofNodes::Print(Option_t* option) const
{
   // Description: Print node information.

   TIter nxk(fNodes);
   TObject *key = 0;
   while ((key = nxk()) != 0) {
      TList *node = dynamic_cast<TList *>(fNodes->GetValue(key));
      if (node) {
         node->Print(option);
      } else {
         Warning("Print", "could not get list for node '%s'", key->GetName()); 
      }
   }
}
