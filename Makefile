
TARGET ?= localhost

.PHONY: all dataHub appInfoStub sensor actuator
all: dataHub appInfoStub sensor actuator

dataHub:
	mkapp -t $(TARGET) dataHub.adef -i $(LEGATO_ROOT)/interfaces/supervisor

appInfoStub:
	mkapp -t $(TARGET) test/appInfoStub.adef -i $(LEGATO_ROOT)/interfaces/supervisor -i $(CURDIR)

sensor:
	mkapp -t $(TARGET) test/sensor.adef -i $(PWD) -s components -i components/periodicSensor

actuator:
	mkapp -t $(TARGET) test/actuator.adef -i $(PWD)

.PHONY: clean
clean:
	rm -rf _build* *.update docs backup

DHUB = _build_dataHub/$(TARGET)/app/dataHub/staging/read-only/bin/dhub

.PHONY: start stop
start: stop all
	# LE_LOG_LEVEL=DEBUG startlegato
	startlegato
	sdir bind "<$(USER)>.le_appInfo" "<$(USER)>.le_appInfo"
	sdir bind "<$(USER)>.sensord.sensor.io" "<$(USER)>.io"
	sdir bind "<$(USER)>.sensord.periodicSensor.dhubIO" "<$(USER)>.io"
	sdir bind "<$(USER)>.actuatord.actuator.io" "<$(USER)>.io"
	sdir bind "<$(USER)>.dhubToolAdmin" "<$(USER)>.admin"
	sdir bind "<$(USER)>.dhubToolIo" "<$(USER)>.io"
	sdir bind "<$(USER)>.dhubToolQuery" "<$(USER)>.query"
	test/supervisor
	$(DHUB) set backupPeriod temp 5
	$(DHUB) set bufferSize temp 100
	$(DHUB) set default /app/sensor/counter/period 0.25
	$(DHUB) set source /app/sensor/temperature/trigger /app/sensor/counter/value
	$(DHUB) set source /obs/temp /app/sensor/temperature/value
	$(DHUB) set default /app/sensor/counter/enable true

stop:
	stoplegato

IFGEN_FLAGS = --gen-interface --output-dir _build_docs

.PHONY: docs
docs:
	mkdir -p _build_docs
	ifgen $(IFGEN_FLAGS) admin.api
	ifgen $(IFGEN_FLAGS) io.api
	ifgen $(IFGEN_FLAGS) query.api
	doxygen
