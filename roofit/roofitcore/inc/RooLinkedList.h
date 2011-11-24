/*****************************************************************************
 * Project: RooFit                                                           *
 * Package: RooFitCore                                                       *
 *    File: $Id: RooLinkedList.h,v 1.15 2007/05/11 09:11:30 verkerke Exp $
 * Authors:                                                                  *
 *   WV, Wouter Verkerke, UC Santa Barbara, verkerke@slac.stanford.edu       *
 *   DK, David Kirkby,    UC Irvine,         dkirkby@uci.edu                 *
 *                                                                           *
 * Copyright (c) 2000-2005, Regents of the University of California          *
 *                          and Stanford University. All rights reserved.    *
 *                                                                           *
 * Redistribution and use in source and binary forms,                        *
 * with or without modification, are permitted according to the terms        *
 * listed in LICENSE (http://roofit.sourceforge.net/license.txt)             *
 *****************************************************************************/
#ifndef ROO_LINKED_LIST
#define ROO_LINKED_LIST

#include "TNamed.h"
#include "RooLinkedListElem.h"
#include "RooHashTable.h"
#include <list>
class RooLinkedListIter ;
class RooFIter ;
class TIterator ;
class RooAbsArg ;

class RooLinkedList : public TObject {
public:
  // Constructor
  RooLinkedList(Int_t htsize=0) ;

  // Copy constructor
  RooLinkedList(const RooLinkedList& other) ;

  virtual TObject* Clone(const char* =0) const { 
    return new RooLinkedList(*this) ;
  }

  // Assignment operator
  RooLinkedList& operator=(const RooLinkedList& other) ;

  Int_t getHashTableSize() const {
    // Return size of hash table
    return _htableName ? _htableName->size() : 0 ;
  }

  void setHashTableSize(Int_t size) ;

  // Destructor
  virtual ~RooLinkedList() ;

  Int_t GetSize() const { return _size ; }

  virtual void Add(TObject* arg) { Add(arg,1) ; }
  virtual Bool_t Remove(TObject* arg) ;
  TObject* At(Int_t index) const ;
  Bool_t Replace(const TObject* oldArg, const TObject* newArg) ;
  TIterator* MakeIterator(Bool_t dir=kTRUE) const ;
  RooLinkedListIter iterator(Bool_t dir=kTRUE) const ;
  RooFIter fwdIterator() const ; 

  void Clear(Option_t *o=0) ;
  void Delete(Option_t *o=0) ;
  TObject* find(const char* name) const ;
  RooAbsArg* findArg(const RooAbsArg*) const ;
  TObject* FindObject(const char* name) const ; 
  TObject* FindObject(const TObject* obj) const ;
  Int_t IndexOf(const char* name) const ;
  Int_t IndexOf(const TObject* arg) const ;
  TObject* First() const {
    return _first?_first->_arg:0 ;
  }

  void Print(const char* opt) const ;
  void Sort(Bool_t ascend=kTRUE) ;
  
  const char* GetName() const { return _name.Data() ; }
  void SetName(const char* name) { _name = name ; }

protected:  

  RooLinkedListElem* createElement(TObject* obj, RooLinkedListElem* elem=0) ;
  void deleteElement(RooLinkedListElem*) ;


  friend class RooLinkedListIter ;
  friend class RooFIter ;

  virtual void Add(TObject* arg, Int_t refCount) ;

  void swapWithNext(RooLinkedListElem* elem) ;

  RooLinkedListElem* findLink(const TObject* arg) const ;
    
  Int_t _hashThresh ;          //  Size threshold for hashing
  Int_t _size ;                //  Current size of list
  RooLinkedListElem*  _first ; //! Link to first element of list
  RooLinkedListElem*  _last ;  //! Link to last element of list
  RooHashTable*       _htableName ; //! Hash table by name 
  RooHashTable*       _htableLink ; //! Hash table by link pointer

  Int_t _curStoreSize ; //!
  Int_t _curStoreUsed ; //!
  std::list<std::pair<Int_t,RooLinkedListElem*> > _storeList ; //!
  RooLinkedListElem* _curStore ; //!

  TString             _name ; 

  ClassDef(RooLinkedList,2) // Doubly linked list for storage of RooAbsArg objects
};




#endif
