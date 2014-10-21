#ifndef PIXTESTHIGHRATE_H
#define PIXTESTHIGHRATE_H

#include "PixTest.hh"
#include "PHCalibration.hh"

#include <TProfile2D.h>


class DLLEXPORT PixTestHighRate: public PixTest {
public:
  PixTestHighRate(PixSetup *, std::string);
  PixTestHighRate();
  virtual ~PixTestHighRate();
  virtual bool setParameter(std::string parName, std::string sval); 
  void init(); 
  void setToolTips();
  void bookHist(std::string); 

  void runCommand(std::string command); 
  void doTest();
  void doXPixelAlive();
  void doRunDaq(); 

  void doHitMap(int nseconds, std::vector<TH2D*>);
  void fillMap(std::vector<TH2D*>);
  void maskHotPixels(); 
 
  //  bool setTrgFrequency(uint8_t TrgTkDel, int triggerFreq = 20);

private:

  int      fParTriggerFrequency;
  int      fParRunSeconds; 
  bool     fParFillTree;
  bool	   fParDelayTBM;
  uint16_t fParNtrig; 
  int      fParVcal; 

  bool          fPhCalOK;
  PHCalibration fPhCal;

  bool    fDaq_loop;
  
  std::vector<TH2D*> fHitMap;
  std::vector<std::vector<std::pair<int, int> > > fHotPixels;
  
  ClassDef(PixTestHighRate, 1)

};
#endif
