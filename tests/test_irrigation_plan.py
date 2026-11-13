"""Host policy and integration contract checks. Never connects to devices."""
import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class PlanTests(unittest.TestCase):
 def test_policy(self):
  with tempfile.TemporaryDirectory() as d:
   exe=str(pathlib.Path(d)/'policy')
   subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(ROOT/'src'),str(ROOT/'tests/test_irrigation_plan.cpp'),'-o',exe],check=True)
   subprocess.run([exe],check=True)
 def test_handoff_and_safety_wiring(self):
  main=(ROOT/'src/main.cpp').read_text();task=(ROOT/'src/task_Watering.h').read_text();impl=(ROOT/'src/irrigation_plan_impl.h').read_text()
  self.assertIn('settings.grow.currentPhase == 3',task)
  self.assertIn('error = "drying_phase"',main);self.assertIn('error = "local_plan_owns_schedule"',main)
  self.assertIn('irrigationPlanInit();',main);self.assertIn('irrigationPlanTick();',main)
  self.assertIn('irrigationPlanFactoryReset() && preferences.clear()',main)
  self.assertIn('irrigationPlanCycleFinished(true)',task);self.assertIn('pump_off_verify_failed',task)
  self.assertIn('"restart_interrupted"',impl);self.assertIn('Never overwrite unacknowledged events',impl)
  self.assertIn('lpPersist())return;lpOwns.store(true)',impl)
  callback=impl[impl.index('void lpHandle('):impl.index('bool lpParseLight(')]
  self.assertNotIn('lpPersist()',callback);self.assertNotIn('setRelay(',callback)
  auth=impl[impl.index('bool lpAuthenticate'):impl.index('void lpHandle')]
  for name in ['source_ip_not_allowed','invalid_auth_headers_or_timestamp','bad_signature','nonce_cache_full','replayed_nonce']:
   self.assertIn(name,auth)
  self.assertIn('String("RESPONSE\\n")',impl);self.assertIn('r->url()',auth)
if __name__=='__main__':unittest.main()
