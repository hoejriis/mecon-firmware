// The remaining radio preferences, from the shared radio CLI (CommonRadioPrefs). Every one that changes how the radio
// listens or sends needs a confirmation from a caller other than the cable. dutycycle is af seen as a percentage.
// Included in both key tables of IfaceConfig.cpp.
  {"af", K_STRING, 1, 8, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get af", F_WHOLE, "set af %s", "airtime-factor"},
  {"int.thresh", K_INT, 0, 255, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get int.thresh", F_WHOLE, "set int.thresh %s"},
  {"cad", K_BOOL, 0, 1, nullptr, KF_WRITABLE | KF_ONOFF | KF_CONFIRM, 0, "get cad", F_WHOLE, "set cad %s"},
  {"radio.rxgain", K_BOOL, 0, 1, nullptr, KF_WRITABLE | KF_ONOFF | KF_CONFIRM, 0, "get radio.rxgain", F_WHOLE,
   "set radio.rxgain %s"},
  {"rxdelay", K_STRING, 1, 8, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get rxdelay", F_WHOLE, "set rxdelay %s", "rx-delay"},
  {"txdelay", K_STRING, 1, 8, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get txdelay", F_WHOLE, "set txdelay %s", "tx-delay"},
  {"direct.txdelay", K_STRING, 1, 8, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get direct.txdelay", F_WHOLE,
   "set direct.txdelay %s", "tx-delay"},
  {"agc.reset.interval", K_INT, 0, 1020, nullptr, KF_WRITABLE, 0, "get agc.reset.interval", F_WHOLE,
   "set agc.reset.interval %s"},
  {"path.hash.mode", K_INT, 0, 2, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get path.hash.mode", F_WHOLE,
   "set path.hash.mode %s"},
  {"multi.acks", K_INT, 0, 1, nullptr, KF_WRITABLE, 0, "get multi.acks", F_WHOLE, "set multi.acks %s"},
