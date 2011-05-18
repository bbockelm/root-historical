// @(#)root/geom:$Id$
// Author: Andrei Gheata   04/02/02

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TGeoVoxelFinder
#define ROOT_TGeoVoxelFinder

#ifndef ROOT_TObject
#include "TObject.h"
#endif

class TGeoVolume;

/*************************************************************************
 * TGeoVoxelFinder - finder class handling voxels 
 *  
 *************************************************************************/

class TGeoVoxelFinder : public TObject
{
public:
enum EVoxelsType {
   kGeoInvalidVoxels = BIT(15),
   kGeoRebuildVoxels = BIT(16)
};
protected:
   TGeoVolume      *fVolume;          // volume to which applies

   Int_t             fNcandidates;    // ! number of candidates
   Int_t             fCurrentVoxel;   // ! index of current voxel in sorted list
   Int_t             fIbx;            // number of different boundaries on X axis
   Int_t             fIby;            // number of different boundaries on Y axis
   Int_t             fIbz;            // number of different boundaries on Z axis
   Int_t             fNboxes;         // length of boxes array
   Int_t             fNox;            // length of array of X offsets
   Int_t             fNoy;            // length of array of Y offsets
   Int_t             fNoz;            // length of array of Z offsets
   Int_t             fNex;            // length of array of X extra offsets
   Int_t             fNey;            // length of array of Y extra offsets
   Int_t             fNez;            // length of array of Z extra offsets
   Int_t             fNx;             // length of array of X voxels
   Int_t             fNy;             // length of array of Y voxels
   Int_t             fNz;             // length of array of Z voxels
   Int_t             fPriority[3];    // priority for each axis
   Int_t             fSlices[3];      // ! slice indices for current voxel
   Int_t             fInc[3];         // ! slice index increment
   Double_t          fInvdir[3];      // ! 1/current director cosines
   Double_t          fLimits[3];      // limits on X,Y,Z
   Double_t         *fBoxes;          //[fNboxes] list of bounding boxes
   Double_t         *fXb;             //[fIbx] ordered array of X box boundaries
   Double_t         *fYb;             //[fIby] ordered array of Y box boundaries
   Double_t         *fZb;             //[fIbz] ordered array of Z box boundaries
   Int_t            *fOBx;            //[fNox] offsets of daughter indices for slices X
   Int_t            *fOBy;            //[fNoy] offsets of daughter indices for slices Y
   Int_t            *fOBz;            //[fNoz] offsets of daughter indices for slices Z
   Int_t            *fOEx;            //[fNox] offsets of extra indices for slices X
   Int_t            *fOEy;            //[fNoy] offsets of extra indices for slices Y
   Int_t            *fOEz;            //[fNoz] offsets of extra indices for slices Z
   Int_t            *fExtraX;         //[fNex] indices of extra daughters in X slices
   Int_t            *fExtraY;         //[fNey] indices of extra daughters in Y slices
   Int_t            *fExtraZ;         //[fNez] indices of extra daughters in Z slices
   Int_t            *fNsliceX;        //[fNox] number of candidates in X slice
   Int_t            *fNsliceY;        //[fNoy] number of candidates in Y slice
   Int_t            *fNsliceZ;        //[fNoz] number of candidates in Z slice
   UChar_t          *fIndcX;          //[fNx] array of slices bits on X
   UChar_t          *fIndcY;          //[fNy] array of slices bits on Y
   UChar_t          *fIndcZ;          //[fNz] array of slices bits on Z
   Int_t            *fCheckList;      //! list of candidates
   UChar_t          *fBits1;          //! bits used for list intersection

   TGeoVoxelFinder(const TGeoVoxelFinder&);
   TGeoVoxelFinder& operator=(const TGeoVoxelFinder&);
   
   void                BuildVoxelLimits();
   Int_t              *GetExtraX(Int_t islice, Bool_t left, Int_t &nextra) const;
   Int_t              *GetExtraY(Int_t islice, Bool_t left, Int_t &nextra) const;
   Int_t              *GetExtraZ(Int_t islice, Bool_t left, Int_t &nextra) const;
   Bool_t              GetIndices(Double_t *point);
   Int_t               GetPriority(Int_t iaxis) const {return fPriority[iaxis];}
   Int_t               GetNcandidates() const         {return fNcandidates;}
   Int_t              *GetValidExtra(Int_t *list, Int_t &ncheck);
   Int_t              *GetValidExtra(Int_t n1, UChar_t *array1, Int_t *list, Int_t &ncheck);
   Int_t              *GetValidExtra(Int_t n1, UChar_t *array1, Int_t n2, UChar_t *array2, Int_t *list, Int_t &ncheck);
   Int_t              *GetVoxelCandidates(Int_t i, Int_t j, Int_t k, Int_t &ncheck);
//   Bool_t              Intersect(Int_t n1, Int_t *array1, Int_t n2, Int_t *array2,
//                             Int_t n3, Int_t *array3, Int_t &nf, Int_t *result); 
   Bool_t              Intersect(Int_t n1, UChar_t *array1, Int_t &nf, Int_t *result); 
   Bool_t              Intersect(Int_t n1, UChar_t *array1, Int_t n2, UChar_t *array2,
                             Int_t &nf, Int_t *result); 
   Bool_t              Intersect(Int_t n1, UChar_t *array1, Int_t n2, UChar_t *array2,
                             Int_t n3, UChar_t *array3, Int_t &nf, Int_t *result); 
//   void                IntersectAndStore(Int_t n1, Int_t *array1, Int_t n2, Int_t *array2,
//                             Int_t n3, Int_t *array3);
   Bool_t              IntersectAndStore(Int_t n1, UChar_t *array1); 
   Bool_t              IntersectAndStore(Int_t n1, UChar_t *array1, Int_t n2, UChar_t *array2); 
   Bool_t              IntersectAndStore(Int_t n1, UChar_t *array1, Int_t n2, UChar_t *array2,
                             Int_t n3, UChar_t *array3); 
   void                SortAll(Option_t *option="");
//   Bool_t              Union(Int_t n1, Int_t *array1, Int_t n2, Int_t *array2,
//                             Int_t n3, Int_t *array3);
   Bool_t              Union(Int_t n1, UChar_t *array1);
   Bool_t              Union(Int_t n1, UChar_t *array1, Int_t n2, UChar_t *array2);
   Bool_t              Union(Int_t n1, UChar_t *array1, Int_t n2, UChar_t *array2,
                             Int_t n3, UChar_t *array3);
public :
   TGeoVoxelFinder();
   TGeoVoxelFinder(TGeoVolume *vol);
   virtual ~TGeoVoxelFinder();
   virtual void        CreateCheckList();
   void                DaughterToMother(Int_t id, Double_t *local, Double_t *master) const;
   virtual Double_t    Efficiency();
   virtual Int_t      *GetCheckList(Double_t *point, Int_t &nelem);
   Int_t              *GetCheckList(Int_t &nelem) const {nelem=fNcandidates; return fCheckList;}
//   virtual Bool_t      GetNextIndices(Double_t *point, Double_t *dir);
   virtual Int_t      *GetNextCandidates(Double_t *point, Int_t &ncheck); 
   virtual void        FindOverlaps(Int_t inode) const;
   Bool_t              IsInvalid() const {return TObject::TestBit(kGeoInvalidVoxels);}
   Bool_t              NeedRebuild() const {return TObject::TestBit(kGeoRebuildVoxels);}
   Double_t           *GetBoxes() const {return fBoxes;}
   Bool_t              IsSafeVoxel(Double_t *point, Int_t inode, Double_t minsafe) const;
   virtual void        Print(Option_t *option="") const;
   void                PrintVoxelLimits(Double_t *point) const;
   void                SetInvalid(Bool_t flag=kTRUE) {TObject::SetBit(kGeoInvalidVoxels, flag);}
   void                SetNeedRebuild(Bool_t flag=kTRUE) {TObject::SetBit(kGeoRebuildVoxels, flag);}
   virtual Int_t      *GetNextVoxel(Double_t *point, Double_t *dir, Int_t &ncheck);
   virtual void        SortCrossedVoxels(Double_t *point, Double_t *dir);
   virtual void        Voxelize(Option_t *option="");

   ClassDef(TGeoVoxelFinder, 3)                // voxel finder class
};

#endif
