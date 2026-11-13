#include "Runtime.h"
#define private public
#include "TuyaControl.h"
#undef private
#define time mock_time
#include "../../src/TuyaControl.cpp"
#undef time
using Control = TuyaControl;
using Stop = heating_session::Stop;
static NatureNetworkManager network;
static TuyaLocalClient client;
static unsigned cases=0;
void reset() {
  ++cases; fakeMs=1000; fakeEpoch=1800000000; network={}; client={}; WiFi={};
  Preferences::bytes.clear(); Preferences::failRead=false; Preferences::failWrite=false;
  Preferences::failRemove=false; Preferences::ready=true; Preferences::failReadKey.clear();
  TuyaLocalClient::relay=false; TuyaLocalClient::readsFail=false;
  TuyaLocalClient::writesApply=true; TuyaLocalClient::ack=true; TuyaLocalClient::connects=true;
  TuyaLocalClient::onWrites=0; TuyaLocalClient::offWrites=0; TuyaLocalClient::reads=0;
  TuyaLocalClient::readHook={}; TuyaLocalClient::setHook={};
  queueSendFails=false; iterations=0; maxIterations=1; iterationHook={};
}
void prepare(Control& c) {
  c.begin(&network); c.client_=&client; c.storage_ready_=true; c.nvs_open_=true;
  c.state_.storage_ready=true; c.state_.reads_enabled=true; c.state_.control_enabled=true;
  c.state_.relay=Control::Relay::Off; c.state_.connected=true; c.state_.last_confirmed_ms=fakeMs;
}
void on(Control& c, uint32_t duration=1800,uint32_t target=50) {
  assert(c.request(Control::Request::On,nullptr,duration,target)==Control::Accept::Accepted);
  c.execute(Control::Request::On); c.pending_=false;
}
void boot(Control& c) {
  c.begin(&network);
  try { c.run(); } catch (const StopRun&) {}
}
int main() {
  reset(); { Control c; prepare(c); on(c); auto s=c.snapshot();
    assert(s.lease_active && s.relay==Control::Relay::On && s.duration_seconds==1800 && s.target_c==50);
    assert(s.progress==Control::Progress::Confirmed && TuyaLocalClient::onWrites==1);
    auto deadline=s.lease_deadline_epoch;
    assert(c.request(Control::Request::On)==Control::Accept::LeaseActive);
    assert(c.snapshot().lease_deadline_epoch==deadline);
    c.execute(Control::Request::Off); s=c.snapshot();
    assert(!s.lease_active && s.relay==Control::Relay::Off && s.shutdown_reason==Stop::Manual);
  }
  reset(); { Control c; prepare(c); c.updateTemperature(true,51,fakeMs); on(c);
    assert(TuyaLocalClient::onWrites==0 && !c.snapshot().lease_active && c.snapshot().shutdown_reason==Stop::Target);
  }
  reset(); { Control c; prepare(c); c.updateTemperature(true,60,fakeMs-16000); on(c,900,40);
    assert(TuyaLocalClient::onWrites==1 && c.snapshot().duration_seconds==900);
    fakeMs+=900000; assert(c.snapshot().lease_remaining_s==0);
  }
  reset(); { Control c; prepare(c); c.updateTemperature(false,120,fakeMs); on(c);
    assert(TuyaLocalClient::onWrites==1);
  }
  reset(); { Control c; prepare(c); Preferences::failWrite=true; on(c);
    assert(TuyaLocalClient::onWrites==0 && !c.snapshot().lease_active && c.snapshot().progress==Control::Progress::Failed);
  }
  reset(); { Control c; prepare(c); Preferences::failRead=true; on(c);
    assert(TuyaLocalClient::onWrites==0 && c.snapshot().progress==Control::Progress::Failed);
  }
  reset(); { Control c; prepare(c); TuyaLocalClient::ack=false;
    TuyaLocalClient::setHook=[] { TuyaLocalClient::readsFail=true; }; on(c);
    assert(TuyaLocalClient::onWrites==1 && c.snapshot().lease_active && c.snapshot().progress==Control::Progress::Uncertain);
    assert(c.request(Control::Request::On)==Control::Accept::LeaseActive);
    assert(TuyaLocalClient::onWrites==1);
  }
  reset(); { Control c; prepare(c); on(c); Preferences::failRemove=true; c.execute(Control::Request::Off);
    assert(c.snapshot().lease_active && c.snapshot().relay==Control::Relay::Off && c.snapshot().progress==Control::Progress::Uncertain);
    Preferences::failRemove=false; c.execute(Control::Request::Off); assert(!c.snapshot().lease_active);
  }
  reset(); { Control c; prepare(c);
    assert(c.request(Control::Request::On,nullptr,2700,50)==Control::Accept::DurationRestricted);
    assert(c.request(Control::Request::On,nullptr,3600,50)==Control::Accept::DurationRestricted);
    assert(c.request(Control::Request::On,nullptr,900,80)==Control::Accept::InvalidSettings);
    assert(c.request(Control::Request::On)==Control::Accept::Accepted);
    assert(c.request(Control::Request::Off)==Control::Accept::Busy);
  }
  reset(); { Control c; prepare(c); network.state.clock_synced=false;
    assert(c.request(Control::Request::On)==Control::Accept::Clock);
    network.state.clock_synced=true; WiFi.available=false;
    assert(c.request(Control::Request::On)==Control::Accept::Offline);
    WiFi.available=true; c.state_.control_enabled=false;
    assert(c.request(Control::Request::On)==Control::Accept::Disabled);
  }
  reset(); { Control c; prepare(c); on(c); auto deadline=c.snapshot().lease_deadline_epoch;
    fakeMs+=300000; fakeEpoch-=100;
    assert(c.snapshot().lease_remaining_s<=1500 && c.snapshot().lease_deadline_epoch==deadline);
  }
  reset(); { Control c; auto legacy=boiler_policy::makeLease(fakeEpoch-100);
    Preferences p; assert(boiler_policy::saveLease(p,legacy)); boot(c);
    assert(c.snapshot().lease_active && c.snapshot().control_enabled && c.snapshot().target_c==0);
    assert(c.snapshot().lease_deadline_epoch==legacy.deadline);
  }
  reset(); { Control c; auto lease=boiler_policy::makeLease(fakeEpoch-100); Preferences p;
    assert(boiler_policy::saveLease(p,lease)); network.state.clock_synced=false; TuyaLocalClient::relay=true; boot(c);
    assert(TuyaLocalClient::offWrites==1 && TuyaLocalClient::onWrites==0 && !c.snapshot().lease_active);
  }
  reset(); { Control c; Preferences::bytes["lease"]={1,2,3}; TuyaLocalClient::relay=true; boot(c);
    assert(TuyaLocalClient::offWrites==1 && !c.snapshot().lease_active && TuyaLocalClient::onWrites==0);
  }
  reset(); { Control c; auto lease=boiler_policy::makeLease(fakeEpoch-100); Preferences p;
    assert(heating_session::saveRecord(p,heating_session::makeRecord(lease,50),lease));
    assert(boiler_policy::saveLease(p,lease)); WiFi.available=false; network.state.clock_synced=false; boot(c);
    assert(c.snapshot().lease_active && c.snapshot().shutdown_pending && TuyaLocalClient::offWrites==0);
  }
  reset(); { Control c; auto lease=boiler_policy::makeLease(fakeEpoch-100); Preferences p;
    assert(heating_session::saveRecord(p,heating_session::makeRecord(lease,50),lease));
    assert(boiler_policy::saveLease(p,lease)); c.updateTemperature(true,55,fakeMs); TuyaLocalClient::relay=true; boot(c);
    assert(TuyaLocalClient::offWrites==1 && !c.snapshot().lease_active && c.snapshot().shutdown_reason==Stop::Target);
  }
  reset(); { Control c; auto lease=boiler_policy::makeLease(fakeEpoch-1800); Preferences p;
    assert(heating_session::saveRecord(p,heating_session::makeRecord(lease,50),lease));
    assert(boiler_policy::saveLease(p,lease)); c.updateTemperature(true,55,fakeMs); TuyaLocalClient::relay=true; boot(c);
    assert(TuyaLocalClient::offWrites==1 && !c.snapshot().lease_active && c.snapshot().shutdown_reason==Stop::Timer);
  }
  reset(); { Control c; auto lease=boiler_policy::makeLease(fakeEpoch-100); Preferences p;
    assert(heating_session::saveRecord(p,heating_session::makeRecord(lease,50),lease));
    assert(boiler_policy::saveLease(p,lease)); c.updateTemperature(true,55,fakeMs-16000); TuyaLocalClient::relay=true; boot(c);
    assert(TuyaLocalClient::offWrites==0 && c.snapshot().lease_active);
  }
  reset(); { Control c; prepare(c);
    TuyaLocalClient::readHook=[] { network.state.clock_synced=false; }; on(c);
    assert(TuyaLocalClient::onWrites==0 && c.snapshot().progress==Control::Progress::Failed);
  }
  reset(); { Control c; prepare(c); queueSendFails=true;
    assert(c.request(Control::Request::On)==Control::Accept::Busy);
    assert(!c.pending_ && c.snapshot().progress==Control::Progress::Failed && TuyaLocalClient::onWrites==0);
  }
  reset(); { Control c; auto lease=boiler_policy::makeLease(fakeEpoch-100); Preferences p;
    assert(heating_session::saveRecord(p,heating_session::makeRecord(lease,50),lease));
    assert(boiler_policy::saveLease(p,lease)); Preferences::bytes["session"][0]^=1;
    TuyaLocalClient::relay=true; boot(c);
    assert(TuyaLocalClient::offWrites==1 && !c.snapshot().lease_active && TuyaLocalClient::onWrites==0);
  }
  reset(); { Control c; auto lease=boiler_policy::makeLease(fakeEpoch-100); Preferences p;
    assert(heating_session::saveRecord(p,heating_session::makeRecord(lease,50),lease));
    assert(boiler_policy::saveLease(p,lease)); network.state.clock_synced=false; WiFi.available=false;
    TuyaLocalClient::relay=true; maxIterations=2;
    iterationHook=[](int iteration) { if (iteration==2) WiFi.available=true; };
    boot(c); assert(TuyaLocalClient::offWrites==1 && !c.snapshot().lease_active);
  }
  reset(); { Control c; auto lease=boiler_policy::makeLease(fakeEpoch-100); Preferences p;
    assert(boiler_policy::saveLease(p,lease)); maxIterations=2;
    iterationHook=[](int iteration) { if (iteration==2) { fakeEpoch-=50; fakeMs+=100000; } };
    boot(c); assert(c.snapshot().lease_active && c.snapshot().lease_remaining_s<=1600);
  }
  reset(); { Control c; prepare(c); Preferences::failReadKey="lease"; on(c);
    assert(TuyaLocalClient::onWrites==0 && c.snapshot().lease_active && c.snapshot().shutdown_pending);
    Preferences::failReadKey.clear(); c.execute(Control::Request::Off); assert(!c.snapshot().lease_active);
  }
  reset(); { Control c; auto lease=boiler_policy::makeLease(fakeEpoch-100); Preferences p;
    assert(boiler_policy::saveLease(p,lease)); network.state.clock_synced=false;
    TuyaLocalClient::relay=true; TuyaLocalClient::readsFail=true; maxIterations=3;
    iterationHook=[](int iteration) { if (iteration>1) fakeMs+=15000; };
    boot(c); assert(TuyaLocalClient::offWrites==3 && c.snapshot().lease_active && c.snapshot().shutdown_pending);
  }
  reset(); { Control c; auto lease=boiler_policy::makeLease(fakeEpoch-100); Preferences p;
    assert(heating_session::saveRecord(p,heating_session::makeRecord(lease,50),lease));
    assert(boiler_policy::saveLease(p,lease)); c.updateTemperature(true,55,fakeMs);
    WiFi.available=false; TuyaLocalClient::relay=true; maxIterations=3;
    iterationHook=[](int iteration) { if (iteration==2) fakeMs+=20000; if (iteration==3) WiFi.available=true; };
    boot(c); assert(TuyaLocalClient::offWrites==1 && !c.snapshot().lease_active && c.snapshot().shutdown_reason==Stop::Target);
  }
  reset(); { Control c; Preferences::ready=false; TuyaLocalClient::relay=true; boot(c);
    assert(!c.snapshot().storage_ready && c.snapshot().lease_active && c.snapshot().shutdown_pending);
    assert(TuyaLocalClient::onWrites==0 && TuyaLocalClient::offWrites==0);
    assert(!c.snapshot().control_enabled);
    c.clearLease(); assert(c.snapshot().lease_active);
  }
  reset(); { Control c; boot(c); // No legacy marker and no USB calls.
    const auto s=c.snapshot();
    assert(s.monitor_armed && s.reads_enabled && s.control_enabled && s.storage_ready);
    assert(!s.lease_active && !s.shutdown_pending);
    assert(TuyaLocalClient::onWrites==0 && TuyaLocalClient::offWrites==0);
    assert(!Preferences::bytes.count("monitor"));
    // Exercise the real command admission/verification path after automatic boot.
    c.client_=&client; assert(c.readStatus()); c.updateTemperature(true,25,fakeMs);
    on(c,900,50);
    assert(c.snapshot().relay==Control::Relay::On && c.snapshot().lease_active);
    assert(TuyaLocalClient::onWrites==1);
    assert(c.request(Control::Request::Off)==Control::Accept::Accepted);
    c.execute(Control::Request::Off); c.pending_=false;
    assert(c.snapshot().relay==Control::Relay::Off && !c.snapshot().lease_active);
    assert(TuyaLocalClient::offWrites==1);
  }
  reset(); { Control c; boot(c); c.client_=&client;
    client.disconnect(); WiFi.available=false;
    assert(!c.connectSession());
    assert(c.request(Control::Request::On)==Control::Accept::Offline);
    WiFi.available=true; TuyaLocalClient::connects=false;
    assert(!c.readStatus());
    assert(c.snapshot().control_enabled && TuyaLocalClient::onWrites==0);
    TuyaLocalClient::connects=true; assert(c.readStatus());
    assert(c.snapshot().relay==Control::Relay::Off && c.snapshot().connected);
    c.execute(Control::Request::Reconnect); // Reconnect is a read, never activation.
    assert(c.snapshot().progress==Control::Progress::Confirmed);
    assert(c.snapshot().control_enabled && TuyaLocalClient::onWrites==0);
  }
  reset(); { Control c; boot(c); const auto first=c.snapshot();
    iterations=0; Control rebooted; boot(rebooted);
    const auto second=rebooted.snapshot();
    assert(first.control_enabled && second.control_enabled && second.reads_enabled);
    assert(!second.lease_active && TuyaLocalClient::onWrites==0);
  }
  std::printf("PASS: %u actual TuyaControl production scenarios\n",cases);
}
