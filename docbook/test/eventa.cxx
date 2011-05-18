// @(#)root/test:$Id$
// Author: Rene Brun   10/01/97

{
//  This macro read all events generated by the test program Event
//  provided in $ROOTSYS/test.
//
//  NOTE: Before executing this macro, you must have executed the macro eventload.
//
//  This small program simply counts the number of bytes read and dump
//  the first 3 events.

   gROOT->Reset();

//   Connect file generated in $ROOTSYS/test
   TFile f("Event.root");

//   Read Tree named "T" in memory. Tree pointer is assigned the same name
   TTree *T = (TTree*)f.Get("T");

//   Create a timer object to benchmark this loop
   TStopwatch timer;
   timer.Start();

//   Start main loop on all events
   Event *event = new Event();
   T->SetBranchAddress("event", &event);
   Int_t nevent = T->GetEntries();
   Int_t nb = 0;
   for (Int_t i=0;i<nevent;i++) {
      if(i%50 == 0) printf("Event:%d\n",i);
      nb += T->GetEntry(i);                  //read complete event in memory
      if (i < 3) event->Dump();              //dump the first 3 events
   }

//  Stop timer and print results
   timer.Stop();
   Float_t mbytes = 0.000001*nb;
   Double_t rtime = timer.RealTime();
   Double_t ctime = timer.CpuTime();
   printf("RealTime=%f seconds, CpuTime=%f seconds\n",rtime,ctime);
   printf("You read %f Mbytes/Realtime seconds\n",mbytes/rtime);
   printf("You read %f Mbytes/Cputime seconds\n",mbytes/ctime);
   printf("%d events and %d bytes read.\n",nevent,nb);

   f.Close();
}
