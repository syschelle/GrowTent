import pathlib,subprocess,sys,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
include=pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory() as d:
 exe=str(pathlib.Path(d)/'parser')
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(root/'src'),'-I',str(include),str(root/'tests/test_irrigation_schedule_parser.cpp'),'-o',exe],check=True)
 subprocess.run([exe],check=True)
