SERIALPORT=/dev/ttyACM0

buildidf:
	idf.py build

flash:
	idf.py flash -p ${SERIALPORT}

monflash:
	idf.py flash monitor -p ${SERIALPORT}

menuconfig:
	idf.py menuconfig

