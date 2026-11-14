"""Production VPD calculation, filter independence and runtime integration."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class VpdTests(unittest.TestCase):
    def test_production_filter(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe=str(pathlib.Path(tmp)/'vpd')
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(ROOT/'tests/test_vpd_calculation.cpp'),'-o',exe],check=True)
            subprocess.run([exe],check=True)
    def test_runtime_wiring(self):
        runtime=(ROOT/'src/runtime.h').read_text()
        sample=runtime[runtime.index('const bool rawValid'):runtime.index('// === JSON BUILDING')]
        self.assertEqual(sample.count('calcControlVPD('),1)
        self.assertIn('cur.vpdRawKpa = calcVPD(cur.temperatureRawC, offsetLeafTemperature, cur.humidityRawPct);',sample)
        self.assertIn('cur.vpdSmoothedKpa = calcControlVPD(cur.temperatureSmoothedC, offsetLeafTemperature, cur.humiditySmoothedPct);',sample)
        self.assertLess(sample.index('updateSmoothedClimate(rawTemp, rawHum);'),sample.index('calcControlVPD('))
        self.assertIn('cur.vpdKpa       = cur.vpdSmoothedKpa;',sample)
        self.assertIn('getAdaptiveAlpha(tempDiff, 0.15f, 0.40f, 0.8f)',runtime)
        self.assertIn('getAdaptiveAlpha(humDiff, 0.15f, 0.35f, 3.0f)',runtime)
if __name__=='__main__': unittest.main()
