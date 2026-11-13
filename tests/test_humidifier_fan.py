"""Fan policy and integration checks. No hardware/network access."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class FanTests(unittest.TestCase):
 def test_timer(self):
  with tempfile.TemporaryDirectory() as tmp:
   exe=str(pathlib.Path(tmp)/'fan')
   subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(ROOT/'tests/test_humidifier_fan.cpp'),'-o',exe],check=True)
   subprocess.run([exe],check=True)
 def test_integration_boundaries(self):
  f=(ROOT/'src/function.h').read_text();m=(ROOT/'src/main.cpp').read_text();impl=(ROOT/'src/humidifier_fan_impl.h').read_text()
  worker=(ROOT/'src/task_CheckShellyStatus.h').read_text()
  self.assertIn('humidifierFanRefreshConfig();',worker)
  self.assertIn('"/settings/relay/0"',impl);self.assertIn('"/rpc/Switch.GetConfig?id=0"',impl)
  self.assertIn('irrigationPumpSlotFromRelayIndex(relay-1)<0',impl)
  self.assertIn('settings.heating.Relay==relay',impl)
  self.assertIn('if (humidifierFanOwnsRelay(i)) continue;',f)
  self.assertIn('humidifierFanOwnsRelay(heatRelay-1)',f)
  self.assertIn('isValidHeatingOutputConfig(pending.sourceType,pending.relay)',m)
  self.assertIn('generation!=hfGeneration',impl)
  callback=impl[impl.index('void hfHandle('):impl.index('bool humidifierFanOwnsRelay')]
  self.assertNotIn('digitalWrite(',callback);self.assertNotIn('putInt(',callback)
  result=impl[impl.index('void humidifierFanCommandResult'):impl.index('void humidifierFanRefreshConfig')]
  self.assertNotIn('digitalWrite(',result)
  self.assertLess(m.index('humidifierFanTick(); // Force OFF'),m.index('if (processOtaExclusivePlatformShutdown())'))
if __name__=='__main__': unittest.main()
