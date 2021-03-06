#include <iostream>
#include <bitset>
#include <stdlib.h>
#include <algorithm>

#include <TStopwatch.h>
#include <TMarker.h>
#include <TStyle.h>
#include <TBox.h>
#include <TMath.h>

#include "PixTestTiming.hh"
#include "PixUtil.hh"
#include "timer.h"
#include "log.h"
#include "helper.h"
#include "constants.h"
#include "math.h"

using namespace std;
using namespace pxar;

ClassImp(PixTestTiming)

//------------------------------------------------------------------------------
PixTestTiming::PixTestTiming(PixSetup *a, std::string name) : PixTest(a, name)
{
  PixTest::init();
  init();
  fTrigBuffer=3;
}

//------------------------------------------------------------------------------
PixTestTiming::PixTestTiming() : PixTest() {}

bool PixTestTiming::setParameter(string parName, string sval) {
  bool found(false);
  std::transform(parName.begin(), parName.end(), parName.begin(), ::tolower);
  for (unsigned int i = 0; i < fParameters.size(); ++i) {
    if (fParameters[i].first == parName) {
      found = true;
      if (!parName.compare("fastscan")) {
        PixUtil::replaceAll(sval, "checkbox(", "");
        PixUtil::replaceAll(sval, ")", "");
        fFastScan = atoi(sval.c_str());
        LOG(logDEBUG) << "fFastScan: " << fFastScan;
        setToolTips();
      }
      if (!parName.compare("targetclk")) {
        fTargetClk = atoi(sval.c_str());
        LOG(logDEBUG) << "PixTestTiming::PixTest() targetclk = " << fTargetClk;
      }
      if (!parName.compare("ntrig")) {
        fNTrig = atoi(sval.c_str());
        LOG(logDEBUG) << "PixTestTiming::PixTest() ntrig = " << fNTrig;
      }
      break;
    }
  }
  return found;
}

void PixTestTiming::init()
{
  LOG(logDEBUG) << "PixTestTiming::init()";

  fDirectory = gFile->GetDirectory(fName.c_str());
  if (!fDirectory)
    fDirectory = gFile->mkdir(fName.c_str());
  fDirectory->cd();
}

void PixTestTiming::setToolTips()
{
  fTestTip = string(Form("scan testboard parameter settings and check for valid readout\n")
                    + string("TO BE IMPLEMENTED!!"));  //FIXME
  fSummaryTip = string("summary plot to be implemented");  //FIXME
}

void PixTestTiming::bookHist(string name)
{
  fDirectory->cd();
  LOG(logDEBUG) << "nothing done with " << name;
}

PixTestTiming::~PixTestTiming()
{
  LOG(logDEBUG) << "PixTestTiming dtor";
  std::list<TH1*>::iterator il;
  fDirectory->cd();
  for (il = fHistList.begin(); il != fHistList.end(); ++il)
    {
      LOG(logINFO) << "Write out " << (*il)->GetName();
      (*il)->SetDirectory(fDirectory);
      (*il)->Write();
    }
}

// ----------------------------------------------------------------------
void PixTestTiming::doTest() {

  fDirectory->cd();
  PixTest::update();
  bigBanner(Form("PixTestTiming::doTest()"));

  ClkSdaScan();
  TH1 *h1 = (*fDisplayedHist);
  h1->Draw(getHistOption(h1).c_str());
  PixTest::update();

  PhaseScan();
  h1 = (*fDisplayedHist);
  h1->Draw(getHistOption(h1).c_str());
  PixTest::update();

  // -- save DACs!
  saveParameters();
  TimingTest();
  
  LOG(logINFO) << "PixTestTiming::doTest() done";
  dutCalibrateOff();
}

//------------------------------------------------------------------------------
void PixTestTiming::ClkSdaScan() {


  // Start test timer
  timer t;

  cacheDacs();
  fDirectory->cd();
  PixTest::update();
  banner(Form("PixTestTiming::ClkSdaScan()"));

  //Make a histogram
  TH2D *h1(0);
  h1 = bookTH2D("ClkSdaScan","ClkSdaScan", 20, -0.5, 19.5, 20, -0.5, 19.5);
  h1->SetDirectory(fDirectory);
  setTitles(h1, "Clk", "Sda");
  fHistOptions.insert(make_pair(h1, "colz"));

  //Turn of Vana
  fApi->setDAC("vana", 0);
  pxar::mDelay(2000);
  double IA = fApi->getTBia();

  //Scan the Clock and SDA to find the working values. iclk starts at fTargetClk and ends at fTargetClk-1. Both sda and clk ranges are limited to 0-19.
  int GoodSDA = -1;
  int GoodClk = -1;
  map<int, vector<int> > goodclksdalist;
  for (int i = 0; i < 20; i++) {
    int iclk = (i+fTargetClk) % 20;
    vector<int> goodsdalist;
    for (int j = 0; j < 20; j++) {
      int isda = (j+iclk+15) % 20;
      fApi->setTestboardDelays(getDelays(iclk,isda));
      LOG(logDEBUG) << "Checking Clk: " << iclk << " Sda: " << isda;
      fApi->setDAC("vana", 70);
      pxar::mDelay(10);
      double NewIA = fApi->getTBia();
      if (TMath::Abs(NewIA-IA) > 0.02) {
        h1->Fill(iclk,isda); //Need 20mA change in current to see if ROCs are programmable
        GoodClk=iclk;
        GoodSDA=isda;
        goodsdalist.push_back(isda);
        if (fFastScan) break;
      }
      fApi->setDAC("vana", 0);
      pxar::mDelay(10);
    }
    goodclksdalist[iclk]=goodsdalist;
    if (fFastScan && GoodClk != -1) break;
  }

  //Overly complicated algorithm to figure out the best SDA.
  //Normally there are 7 sda settings that work, and this selects the middle one.
  //It's completcated because the working SDA settings can be 0, 1, 2, 3, 4, 18, and 19. The center most value is 1.
  if (GoodClk != -1) {
    if (!fFastScan) {
      GoodClk = -1;
      for (int i = 0; i < 20; i++) {
        int iclk = (i+fTargetClk) % 20;
        if (goodclksdalist.count(iclk)) {
          GoodClk = iclk;
          vector<int> goodsdalist = goodclksdalist[iclk];
          if (goodsdalist.size() == 1) {
            GoodSDA=goodsdalist[0];
          } else {
            sort(goodsdalist.begin(),goodsdalist.end());
            for (size_t isda=1; isda<goodsdalist.size(); isda++) if (TMath::Abs(goodsdalist[isda]-goodsdalist[isda-1])>1) goodsdalist[isda] -= 20;
            sort(goodsdalist.begin(),goodsdalist.end());
            GoodSDA=goodsdalist[goodsdalist.size()/2];
            if (GoodSDA<0) GoodSDA+=20;
            break;
          }
        }
      }
    }

    //Print out good values and set them on the test board.
    vector<pair<string,uint8_t> > GoodDelays = getDelays(GoodClk,GoodSDA);
    LOG(logINFO) << "SDA delay found at clock:" << GoodClk;
    for(vector<pair<string,uint8_t> >::iterator idelay = GoodDelays.begin(); idelay != GoodDelays.end(); ++idelay) {
      LOG(logINFO) << idelay->first << ":    " << int(idelay->second);
      fPixSetup->getConfigParameters()->setTbParameter(idelay->first, idelay->second);
    }
    fApi->setTestboardDelays(GoodDelays);
  } else LOG(logERROR) << "No working SDA setting found!";

  //Draw the plot
  h1->Draw("colz");
  fHistList.push_back(h1);
  fDisplayedHist = find(fHistList.begin(), fHistList.end(), h1);
  PixTest::update();
  restoreDacs();

  // Print timer value:
  LOG(logINFO) << "Test took " << t << " ms.";
  LOG(logINFO) << "PixTestTiming::ClkSdaScan() done.";

  dutCalibrateOff();
}

//------------------------------------------------------------------------------
void PixTestTiming::PhaseScan() {

  // Start test timer
  timer t;

  fApi->_dut->testAllPixels(false);
  fApi->_dut->maskAllPixels(true);
  
  banner(Form("PixTestTiming::PhaseScan()"));
  cacheTBMDacs();

  //Make histograms
  TH2D *h1(0);
  h1 = bookTH2D("TBMPhases","Functional TBM Phases", 8, -0.5, 7.5, 8, -0.5, 7.5);
  h1->SetDirectory(fDirectory);
  setTitles(h1, "160MHz Phase", "400 MHz Phase");
  fHistOptions.insert(make_pair(h1, "colz"));
  h1->SetMinimum(0);

  //Make histograms
  TH2D *h2(0);
  h2 = bookTH2D("TBMPhaseMap","TBM Phase Map", 8, -0.5, 7.5, 8, -0.5, 7.5);
  h2->SetDirectory(fDirectory);
  setTitles(h2, "160MHz Phase", "400 MHz Phase");
  fHistOptions.insert(make_pair(h2, "colz"));
  h2->SetMinimum(0);

  TH2D *h3(0);
  vector<TH2D*> rocdelayhists;
  
  TLogLevel UserReportingLevel = Log::ReportingLevel();
  size_t nTBMs = fApi->_dut->getNTbms();
  uint16_t period = 200;
  vector<rawEvent> daqRawEv;
  vector<Event> daqEv;

  int NTimings = 0;
  int NFunctionalTimings = 0;
  int NFunctionalTBMPhases = 0;

  fApi->daqStart();
  for (int iclk160 = 0; iclk160 < 8; iclk160++) {
    for (int iclk400 = 0; iclk400 < 8; iclk400++) {
      uint8_t delaysetting = iclk160<<5 | iclk400<<2;
      LOG(logDEBUG) << "160MHz Phase: " << iclk160 << " 400MHz Phase: " << iclk400 << " Delay Setting: " << bitset<8>(delaysetting).to_string();
      fApi->setTbmReg("basee", delaysetting, 0); //Set TBM 160-400 MHz Clock Phase
      int NFunctionalROCPhases = 0;
      fApi->daqStart();
      for (int ithtdelay = 0; ithtdelay < 4; ithtdelay++) {
        //if (ithtdelay==2) continue;
        h3 = bookTH2D(Form("ROCDelayScan_%d_%d",delaysetting/4,ithtdelay),Form("ROC Delay Scan: 160MHz Phase = %d 400MHz Phase = %d THT Delay = %d",iclk160,iclk400,ithtdelay), 8, -0.5, 7.5, 8, -0.5, 7.5);
        h3->SetDirectory(fDirectory);
        setTitles(h3, "ROC Port 0 Delay", "ROC Port 1 Delay");
        h3->SetMinimum(0);
        for (int irocphaseport1 = 0; irocphaseport1 < 8; irocphaseport1++) {
          for (int irocphaseport0 = 0; irocphaseport0 < 8; irocphaseport0++) {
            NTimings++;
            int ROCDelay = (ithtdelay << 6) | (irocphaseport1 << 3) | irocphaseport0;
            LOG(logDEBUG) << "Token Header/Trailer Delay: " << bitset<2>(ithtdelay).to_string() << " ROC Port1: " << bitset<3>(irocphaseport1).to_string() << " ROC Port0: " << bitset<3>(irocphaseport0).to_string() << " ROCDelay Setting: " << bitset<8>(ROCDelay).to_string();
            for (size_t itbm = 0; itbm<nTBMs; itbm++) fApi->setTbmReg("basea", ROCDelay, itbm);
            Log::ReportingLevel() = Log::FromString("QUIET");
            statistics results = getEvents(fNTrig, period, fTrigBuffer);
            Log::ReportingLevel() = UserReportingLevel;
            if (Log::ReportingLevel() >= logDEBUG) results.dump();
            int NEvents = (results.info_events_empty()+results.info_events_valid())/nTBMs;
            if (NEvents==fNTrig) {
              h3->Fill(irocphaseport0,irocphaseport1);
              NFunctionalTimings++;
              NFunctionalROCPhases++;
            }
          }
        }
        if (h3->GetEntries()>0) rocdelayhists.push_back(h3);
      }
      fApi->daqStop();
      if (NFunctionalROCPhases>0) {
        NFunctionalTBMPhases++;
        h1->Fill(iclk160,iclk400);
        h2->Fill(iclk160,iclk400,NFunctionalROCPhases);
      }
    }
  }

  //Draw the plots
  for (size_t ihist = 0; ihist < rocdelayhists.size(); ihist++) {
    h3 = rocdelayhists[ihist];
    h3->Draw("colz");
    fHistList.push_back(h3);
    fHistOptions.insert(make_pair(h3, "colz"));
  }

  h2->Draw("colz");
  fHistList.push_back(h2);
  h1->Draw("colz");
  fHistList.push_back(h1);
  fDisplayedHist = find(fHistList.begin(), fHistList.end(), h1);
  PixTest::update();

  restoreTBMDacs();
  banner(Form("The fraction of total functioning timings is %4.2f%%: %d/%d", float(NFunctionalTimings)/NTimings*100, NFunctionalTimings, NTimings));
  banner(Form("The fraction of functioning TBM Phases is %4.2f%%: %d/%d", float(NFunctionalTBMPhases)/64*100, NFunctionalTBMPhases, 64));

  // Print timer value:
  LOG(logINFO) << "Test took " << t << " ms.";
  LOG(logINFO) << "PixTestTiming::PhaseScan() done.";
  dutCalibrateOff();
}

//------------------------------------------------------------------------------
void PixTestTiming::TBMPhaseScan() {

  // Start test timer
  timer t;

  fApi->_dut->testAllPixels(false);
  fApi->_dut->maskAllPixels(true);

  banner(Form("PixTestTiming::TBMPhaseScan()"));
  cacheTBMDacs();

  //Make histograms
  TH2D *h1(0);
  h1 = bookTH2D("TBMPhaseScan","TBM Phase Scan", 8, -0.5, 7.5, 8, -0.5, 7.5);
  h1->SetDirectory(fDirectory);
  setTitles(h1, "160MHz Phase", "400 MHz Phase");
  fHistOptions.insert(make_pair(h1, "colz"));
  h1->SetMinimum(0);

  TLogLevel UserReportingLevel = Log::ReportingLevel();
  size_t nTBMs = fApi->_dut->getNTbms();
  uint16_t period = 200;
  vector<rawEvent> daqRawEv;
  vector<Event> daqEv;

  fApi->daqStart();
  for (int iclk160 = 0; iclk160 < 8; iclk160++) {
    for (int iclk400 = 0; iclk400 < 8; iclk400++) {
      uint8_t delaysetting = iclk160<<5 | iclk400<<2;
      LOG(logDEBUG) << "160MHz Phase: " << iclk160 << " 400MHz Phase: " << iclk400 << " Delay Setting: " << bitset<8>(delaysetting).to_string();
      fApi->setTbmReg("basee", delaysetting, 0); //Set TBM 160-400 MHz Clock Phase
      fApi->daqStart();
      Log::ReportingLevel() = Log::FromString("QUIET");
      statistics results = getEvents(fNTrig, period, fTrigBuffer);
      Log::ReportingLevel() = UserReportingLevel;
      fApi->daqStop();
      int NEvents = (results.info_events_empty()+results.info_events_valid())/nTBMs;
      if (NEvents==fNTrig) h1->Fill(iclk160,iclk400);
      if (Log::ReportingLevel() >= logDEBUG) results.dump();
    }
  }
  
  //Draw the plots
  h1->Draw("colz");
  fHistList.push_back(h1);
  fDisplayedHist = find(fHistList.begin(), fHistList.end(), h1);
  PixTest::update();

  restoreTBMDacs();

  // Print timer value:
  LOG(logINFO) << "Test took " << t << " ms.";
  LOG(logINFO) << "PixTestTiming::TBMPhaseScan() done.";
  dutCalibrateOff();
}

//------------------------------------------------------------------------------
void PixTestTiming::ROCDelayScan() {

  // Start test timer
  timer t;

  fApi->_dut->testAllPixels(false);
  fApi->_dut->maskAllPixels(true);

  banner(Form("PixTestTiming::ROCDelayScan()"));
  cacheTBMDacs();

  TLogLevel UserReportingLevel = Log::ReportingLevel();
  size_t nTBMs = fApi->_dut->getNTbms();
  uint16_t period = 200;
  vector<rawEvent> daqRawEv;
  vector<Event> daqEv;

  //Make histograms
  TH2D *h1(0);
  vector<TH2D*> rocdelayhists;

  fApi->daqStart();
  for (int ithtdelay = 0; ithtdelay < 4; ithtdelay++) {
    if (ithtdelay==2) continue;
    h1 = bookTH2D(Form("ROCDelayScan%d",ithtdelay),Form("ROC Delay Scan: THT Delay = %d",ithtdelay), 8, -0.5, 7.5, 8, -0.5, 7.5);
    h1->SetDirectory(fDirectory);
    setTitles(h1, "ROC Port 0 Delay", "ROC Port 1 Delay");
    fHistOptions.insert(make_pair(h1, "colz"));
    h1->SetMinimum(0);
    for (int irocphaseport1 = 0; irocphaseport1 < 8; irocphaseport1++) {
      for (int irocphaseport0 = 0; irocphaseport0 < 8; irocphaseport0++) {
        int ROCDelay = (ithtdelay << 6) | (irocphaseport1 << 3) | irocphaseport0;
        LOG(logDEBUG) << "Token Header/Trailer Delay: " << bitset<2>(ithtdelay).to_string() << " ROC Port1: " << bitset<3>(irocphaseport1).to_string() << " ROC Port0: " << bitset<3>(irocphaseport0).to_string() << " ROCDelay Setting: " << bitset<8>(ROCDelay).to_string();
        for (size_t itbm = 0; itbm<nTBMs; itbm++) fApi->setTbmReg("basea", ROCDelay, itbm);
        Log::ReportingLevel() = Log::FromString("QUIET");
        statistics results = getEvents(fNTrig, period, fTrigBuffer);
        Log::ReportingLevel() = UserReportingLevel;
        int NEvents = (results.info_events_empty()+results.info_events_valid())/nTBMs;
        if (NEvents==fNTrig) h1->Fill(irocphaseport0,irocphaseport1);
        if (Log::ReportingLevel() >= logDEBUG) results.dump();
      }
    }
    rocdelayhists.push_back(h1);
  }
  fApi->daqStop();
  
  //Draw plots
  for (size_t ihist = 0; ihist < rocdelayhists.size(); ihist++) {
    h1 = rocdelayhists[ihist];
    h1->Draw("colz");
    fHistList.push_back(h1);
    fDisplayedHist = find(fHistList.begin(), fHistList.end(), h1);
    PixTest::update();
  }

  restoreTBMDacs();
  
  // Print timer value:
  LOG(logINFO) << "Test took " << t << " ms.";
  LOG(logINFO) << "PixTestTiming::ROCDelayScan() done.";
  dutCalibrateOff();
}

//------------------------------------------------------------------------------
void PixTestTiming::TimingTest() {

  // Start test timer
  timer t;

  fApi->_dut->testAllPixels(false);
  fApi->_dut->maskAllPixels(true);

  banner(Form("PixTestTiming::TimingTest()"));

  int nTBMs = fApi->_dut->getNTbms();
  uint16_t period = 200;
  vector<rawEvent> daqRawEv;
  vector<Event> daqEv;

  fApi->daqStart();
  statistics results = getEvents(fNTrig, period, fTrigBuffer);
  fApi->daqStop();
  int NEvents = (results.info_events_empty()+results.info_events_valid())/nTBMs;
  if (Log::ReportingLevel() >= logDEBUG) results.dump();

  banner(Form("The fraction of properly decoded events is %4.2f%%: %d/%d", float(NEvents)/fNTrig*100, NEvents, fNTrig));
  if (NEvents==fNTrig) banner("Timings are good!");
  else banner("Timings are not good :(", logERROR);
  LOG(logINFO) << "Test took " << t << " ms.";
  LOG(logINFO) << "PixTestTiming::TimingTest() done.";

  dutCalibrateOff();
}

//------------------------------------------------------------------------------
void PixTestTiming::LevelScan() {

  // Start test timer
  timer t;

  fApi->_dut->testAllPixels(false);
  fApi->_dut->maskAllPixels(true);

  fDirectory->cd();
  PixTest::update();
  banner(Form("PixTestTiming::LevelScan()"));

  TLogLevel UserReportingLevel = Log::ReportingLevel();
  uint16_t period = 200;

  //Make a histogram
  TH1D *h1(0);
  h1 = bookTH1D("LevelScan","Level Scan", 16, -0.5, 15.5);
  h1->SetDirectory(fDirectory);
  setTitles(h1, "DTB Level", "Good Events");

  //Get the normal info
  size_t nTBMs = fApi->_dut->getNTbms();

  //Get Intial TBM Parameters
  vector<pair<string, uint8_t> > InitTBMParameters = fPixSetup->getConfigParameters()->getTbParameters();

  vector<uint8_t> GoodLevels;
  vector<rawEvent> daqRawEv;
  vector<Event> daqEv;

  for (uint8_t ilevel=15; ilevel>3; ilevel--){
    LOG(logDEBUG) << "Testing Level: " << int(ilevel);
    fPixSetup->getConfigParameters()->setTbParameter("level", ilevel);
    fApi->setTestboardDelays(fPixSetup->getConfigParameters()->getTbParameters());
    fApi->daqStart();
    Log::ReportingLevel() = Log::FromString("QUIET");
    statistics results = getEvents(fNTrig, period, fTrigBuffer);
    Log::ReportingLevel() = UserReportingLevel;
    fApi->daqStop();
    int NEvents = (results.info_events_empty()+results.info_events_valid())/nTBMs;
    if (NEvents) h1->Fill(int(ilevel), NEvents);
    if (NEvents==fNTrig) GoodLevels.push_back(ilevel);
  }

  if (GoodLevels.size()) {
    uint8_t MeanLevel = 0;
    if (GoodLevels.size()==1) MeanLevel = GoodLevels.front();
    else MeanLevel = GoodLevels[GoodLevels.size()/2]; //Pick the median functional level (hope there's no gaps)
    fPixSetup->getConfigParameters()->setTbParameter("level", MeanLevel);
    fApi->setTestboardDelays(fPixSetup->getConfigParameters()->getTbParameters());
    LOG(logINFO) << "DTB level set to " << int(MeanLevel);
  } else {
    LOG(logERROR) << "No working level found!";
    fApi->setTestboardDelays(InitTBMParameters);
  }

  //Draw the plot
  h1->Draw();
  fHistList.push_back(h1);
  fDisplayedHist = find(fHistList.begin(), fHistList.end(), h1);
  PixTest::update();

  fApi->setPatternGenerator(fPixSetup->getConfigParameters()->getTbPgSettings());
  LOG(logINFO) << "Test took " << t << " ms.";
  LOG(logINFO) << "PixTestTiming::LevelScan() done.";

  dutCalibrateOff();
}

//------------------------------------------------------------------------------
statistics PixTestTiming::getEvents(int NEvents, int period, int buffer) {

  int NStep = 1000000;
  int NLoops = floor((NEvents-1)/NStep);
  int NRemainder = NEvents%NStep;
  if (NRemainder==0) NRemainder=NStep;

  statistics results;
  vector<Event> daqEv;

  if (buffer > 0) {
    vector<rawEvent> daqRawEv;
    fApi->daqTrigger(buffer, period);
    try { daqRawEv = fApi->daqGetRawEventBuffer(); }
    catch(pxar::DataNoEvent &) {}
    for (size_t iEvent=0; iEvent<daqRawEv.size(); iEvent++) LOG(logDEBUG) << "Event: " << daqRawEv[iEvent];
  }
  
  for (int iloop=0; iloop<NLoops; iloop++) {
    LOG(logDEBUG) << "Collecting " << (iloop+1)*NStep << "/" << NEvents << " Triggers";
    fApi->daqTrigger(NStep, period);
    try { daqEv = fApi->daqGetEventBuffer(); }
    catch(pxar::DataNoEvent &) {}
    results += fApi->getStatistics();
  }

  LOG(logDEBUG) << "Collecting " << (NLoops*NStep)+NRemainder << "/" << NEvents << " Triggers";
  fApi->daqTrigger(NRemainder, period);
  try { daqEv = fApi->daqGetEventBuffer(); }
  catch(pxar::DataNoEvent &) {}
  results += fApi->getStatistics();
  
  return results;
}

//------------------------------------------------------------------------------
vector<pair<string,uint8_t> > PixTestTiming::getDelays(uint8_t clk, uint8_t sda) {
  vector<pair<string,uint8_t> > sigdelays;
  sigdelays.push_back(make_pair("clk", clk%20));
  sigdelays.push_back(make_pair("ctr", clk%20));
  sigdelays.push_back(make_pair("sda", sda%20));
  sigdelays.push_back(make_pair("tin", (clk+5)%20));
  return sigdelays;
}

// ----------------------------------------------------------------------
pair <int, int> PixTestTiming::getGoodRegion(TH2D* hist, int hits) {

  if (hist->GetEntries()==0) return make_pair(0,0);

  int MaxGoodRegionSize=0;
  int GoodROCDelay=0;
  for (int startbinx=1; startbinx<=hist->GetNbinsX(); startbinx++) {
    for (int startbiny=1; startbiny<=hist->GetNbinsY(); startbiny++) {
      if (int(hist->GetBinContent(startbinx,startbiny))!=hits) continue;
      for (int regionsize=0; regionsize<8; regionsize++) {
        bool regiongood = true;
        for (int xoffset=0; xoffset<=regionsize && regiongood; xoffset++) {
          for (int yoffset=0; yoffset<=regionsize && regiongood; yoffset++) {
            int checkbinx = (startbinx+xoffset>8) ? startbinx+xoffset-8 : startbinx+xoffset;
            int checkbiny = (startbiny+yoffset>8) ? startbiny+yoffset-8 : startbiny+yoffset;
            if (int(hist->GetBinContent(checkbinx,checkbiny))!=hits) regiongood=false;
          }
        }
        if (regiongood && regionsize+1>MaxGoodRegionSize) {
          MaxGoodRegionSize=regionsize+1;
          GoodROCDelay = (startbinx-1+regionsize/2)%8 | (startbiny-1+regionsize/2)%8<<3;
        }
      }
    }
  }

  return make_pair(MaxGoodRegionSize, GoodROCDelay);

}

// ----------------------------------------------------------------------
void PixTestTiming::saveParameters() {
  LOG(logINFO) << "PixTestTiming:: Write Tbm parameters to file.";
  fPixSetup->getConfigParameters()->writeTbParameterFile();
  for (unsigned int itbm = 0; itbm < fApi->_dut->getNTbms(); itbm += 2) {
    fPixSetup->getConfigParameters()->writeTbmParameterFile(itbm, fApi->_dut->getTbmDACs(itbm), fApi->_dut->getTbmChainLengths(itbm), fApi->_dut->getTbmDACs(itbm+1), fApi->_dut->getTbmChainLengths(itbm+1));
  }
}

// ----------------------------------------------------------------------
void PixTestTiming::runCommand(string command) {
  transform(command.begin(), command.end(), command.begin(), ::tolower);
  LOG(logDEBUG) << "running command: " << command;
  if (!command.compare("clocksdascan")) {
    ClkSdaScan();
    return;
  }
  if (!command.compare("phasescan")) {
    PhaseScan();
    return;
  }
  if (!command.compare("levelscan")) {
    LevelScan();
    return;
  }
  if (!command.compare("tbmphasescan")) {
    TBMPhaseScan();
    return;
  }
  if (!command.compare("rocdelayscan")) {
    ROCDelayScan();
    return;
  }
  if (!command.compare("timingtest")) {
    TimingTest();
    return;
  }
  if (!command.compare("saveparameters")) {
    saveParameters();
    return;
  }
  LOG(logDEBUG) << "did not find command ->" << command << "<-";
}
