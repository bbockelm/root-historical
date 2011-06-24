// @(#)root/roostats:$Id$
// Author: Kyle Cranmer, Lorenzo Moneta, Gregory Schott, Wouter Verkerke, Sven Kreiss
/*************************************************************************
 * Copyright (C) 1995-2008, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//_________________________________________________
/*
BEGIN_HTML
<p>
The p-value of the null for a given test statistic is rigorously defined and
this is the starting point for the following conventions.
</p>

<h3>Conventions used in this class</h3>
<p>
The p-value for the null and alternate are on the <b>same side</b> of the
observed value of the test statistic. This is the more standard
convention and avoids confusion when doing inverted tests.
</p>
<p>
For exclusion, we also want the formula
CLs = CLs+b / CLb to hold which therefore defines our conventions
for CLs+b and CLb. CLs was specifically invented for exclusion
and therefore all quantities need be related through the assignments
as they are for exclusion: <b>CLs+b = p_{s+b}; CLb = p_b</b>. This
is derived by considering the scenarios of a powerful and not powerful
inverted test, where for the not so powerful test, CLs must be
close to one.
</p>
<p>
For results of Hypothesis tests,
CLs has no similar direct interpretation as for exclusion and can
be larger than one.
</p>

END_HTML
*/
//


#ifndef ROOSTATS_HypoTestResult
#define ROOSTATS_HypoTestResult

#ifndef ROOT_TNamed
#include "TNamed.h"
#endif

#ifndef ROOSTATS_RooStatsUtils
#include "RooStats/RooStatsUtils.h"
#endif

#ifndef ROOSTATS_SamplingDistribution
#include "RooStats/SamplingDistribution.h"
#endif

namespace RooStats {

   class HypoTestResult : public TNamed {

   public:
      
      // default constructor
      explicit HypoTestResult(const char* name = 0);

      // constructor from name, null and alternate p values 
      HypoTestResult(const char* name, Double_t nullp, Double_t altp);

      // destructor 
      virtual ~HypoTestResult();

      // add values from another HypoTestResult
      virtual void Append(const HypoTestResult *other);

      // Return p-value for null hypothesis
      virtual Double_t NullPValue() const { return fNullPValue; }

      // Return p-value for alternate hypothesis
      virtual Double_t AlternatePValue() const { return fAlternatePValue; }

      // Convert  NullPValue into a "confidence level"
      virtual Double_t CLb() const { return !fBackgroundIsAlt ? NullPValue() : AlternatePValue(); }

      // Convert  AlternatePValue into a "confidence level"
      virtual Double_t CLsplusb() const { return !fBackgroundIsAlt ? AlternatePValue() : NullPValue(); }

      // CLs is simply CLs+b/CLb (not a method, but a quantity)
      virtual Double_t CLs() const {
         double thisCLb = CLb();
         if (thisCLb == 0) {
            std::cout << "Error: Cannot compute CLs because CLb = 0. Returning CLs = -1\n";
            return -1;
         }
         double thisCLsb = CLsplusb();
         return thisCLsb / thisCLb;
      }

      // familiar name for the Null p-value in terms of 1-sided Gaussian significance
      virtual Double_t Significance() const {return RooStats::PValueToSignificance( NullPValue() ); }

      SamplingDistribution* GetNullDistribution(void) const { return fNullDistr; }
      SamplingDistribution* GetAltDistribution(void) const { return fAltDistr; }
      Double_t GetTestStatisticData(void) const { return fTestStatisticData; }
      Bool_t HasTestStatisticData(void) const;

      void SetAltDistribution(SamplingDistribution *alt);
      void SetNullDistribution(SamplingDistribution *null);
      void SetTestStatisticData(const Double_t tsd);

      void SetPValueIsRightTail(Bool_t pr);
      Bool_t GetPValueIsRightTail(void) const { return fPValueIsRightTail; }

      void SetBackgroundAsAlt(Bool_t l = kTRUE) { fBackgroundIsAlt = l; }
      Bool_t GetBackGroundIsAlt(void) const { return fBackgroundIsAlt; }

      /// The error on the "confidence level" of the null hypothesis
      Double_t CLbError() const;

      /// The error on the "confidence level" of the alternative hypothesis
      Double_t CLsplusbError() const;

      /// The error on the ratio CLs+b/CLb
      Double_t CLsError() const;

      Double_t NullPValueError() const;


      void Print(const Option_t* = "") const {
         // Print out some information about the results
         // Note: use Alt/Null labels for the hypotheses here as the Null
         // might be the s+b hypothesis.

         cout << endl << "Results " << GetName() << ": " << endl;
         if(HasTestStatisticData()  &&  fNullDistr) {
            cout << " - Null p-value = " << NullPValue() << " +/- " << NullPValueError() << endl;
            cout << " - Significance = " << Significance() << " sigma" << endl;
         }
         if(fAltDistr)
            cout << " - Number of Alt toys: " << fAltDistr->GetSize() << std::endl;
         if(fNullDistr)
            cout << " - Number of Null toys: " << fNullDistr->GetSize() << std::endl;
         if(HasTestStatisticData())
            cout << " - Test statistic evaluated on data: " << fTestStatisticData << std::endl;
         if(HasTestStatisticData()  &&  fNullDistr)
            cout << " - CL_b: " << CLb() << " +/- " << CLbError() << std::endl;
         if(HasTestStatisticData()  &&  fAltDistr)
            cout << " - CL_s+b: " << CLsplusb() << " +/- " << CLsplusbError() << std::endl;
         if(HasTestStatisticData()  &&  fAltDistr  &&  fNullDistr)
            cout << " - CL_s: " << CLs() << " +/- " << CLsError()  << std::endl;

         return;
      }

   private:
      void UpdatePValue(const SamplingDistribution* distr, Double_t &pvalue, Double_t &perror,  Bool_t pIsRightTail);


   protected:

      mutable Double_t fNullPValue; // p-value for the null hypothesis (small number means disfavored)
      mutable Double_t fAlternatePValue; // p-value for the alternate hypothesis (small number means disfavored)
      mutable Double_t fNullPValueError; // error of p-value for the null hypothesis (small number means disfavored)
      mutable Double_t fAlternatePValueError; // error of p-value for the alternate hypothesis (small number means disfavored)
      Double_t fTestStatisticData; // result of the test statistic evaluated on data
      SamplingDistribution *fNullDistr;
      SamplingDistribution *fAltDistr;
      Bool_t fPValueIsRightTail;
      Bool_t fBackgroundIsAlt;

      ClassDef(HypoTestResult,2)  // Base class to represent results of a hypothesis test

   };
}


#endif
