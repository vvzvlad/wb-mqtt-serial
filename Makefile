# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/
DEPDIR := .d
$(shell mkdir -p $(DEPDIR) >/dev/null)
DEPFLAGS = -std=c++14 -MT $@ -MMD -MP -MF $(DEPDIR)/$(notdir $*.Td)

POSTCOMPILE = mv -f $(DEPDIR)/$(notdir $*.Td) $(DEPDIR)/$(notdir $*.d)

#CFLAGS=
DEBUG_CFLAGS=-Wall -ggdb -std=c++14 -O0 -I.
NORMAL_CFLAGS=-Wall -std=c++14 -O3 -I.
CFLAGS=$(if $(or $(DEBUG)), $(DEBUG_CFLAGS),$(NORMAL_CFLAGS))
LDFLAGS= -pthread -ljsoncpp -lwbmqtt1

SERIAL_BIN=wb-mqtt-serial
SERIAL_LIBS=
SERIAL_SRCS= \
  register.cpp            \
  poll_plan.cpp           \
  serial_client.cpp       \
  register_handler.cpp    \
  serial_config.cpp       \
  serial_port_driver.cpp  \
  serial_driver.cpp       \
  serial_port.cpp         \
  serial_device.cpp       \
  uniel_device.cpp        \
  s2k_device.cpp          \
  ivtm_device.cpp         \
  crc16.cpp               \
  modbus_common.cpp       \
  modbus_device.cpp       \
  modbus_io_device.cpp    \
  em_device.cpp           \
  milur_device.cpp        \
  mercury230_device.cpp   \
  mercury200_device.cpp   \
  pulsar_device.cpp       \
  bcd_utils.cpp           \
  log.cpp                 \

SERIAL_OBJS=$(SERIAL_SRCS:.cpp=.o)
TEST_SRCS= \
  $(TEST_DIR)/poll_plan_test.o                        \
  $(TEST_DIR)/serial_client_test.o                    \
  $(TEST_DIR)/modbus_expectations_base.o              \
  $(TEST_DIR)/modbus_expectations.o                   \
  $(TEST_DIR)/modbus_test.o                           \
  $(TEST_DIR)/modbus_io_expectations.o                \
  $(TEST_DIR)/modbus_io_test.o                        \
  $(TEST_DIR)/uniel_expectations.o                    \
  $(TEST_DIR)/uniel_test.o                            \
  $(TEST_DIR)/s2k_expectations.o                      \
  $(TEST_DIR)/s2k_test.o                              \
  $(TEST_DIR)/em_test.o                               \
  $(TEST_DIR)/em_integration.o                        \
  $(TEST_DIR)/mercury200_expectations.o               \
  $(TEST_DIR)/mercury200_test.o                       \
  $(TEST_DIR)/mercury230_expectations.o               \
  $(TEST_DIR)/mercury230_test.o                       \
  $(TEST_DIR)/milur_expectations.o                    \
  $(TEST_DIR)/milur_test.o                            \
  $(TEST_DIR)/ivtm_test.o                             \
  $(TEST_DIR)/pulsar_test.o                           \
  $(TEST_DIR)/fake_serial_port.o                      \
  $(TEST_DIR)/fake_serial_device.o                    \
  $(TEST_DIR)/device_templates_file_extension_test.o  \
  $(TEST_DIR)/reconnect_detection_test.o              \
  $(TEST_DIR)/main.o                                  \

TEST_OBJS=$(TEST_SRCS:.cpp=.o)
TEST_LIBS=-lgtest -lpthread -lmosquittopp -lwbmqtt_test_utils
TEST_DIR=test
TEST_BIN=wb-homa-test
SRCS=$(SERIAL_SRCS) $(TEST_SRCS)

.PHONY: all clean test

all : $(SERIAL_BIN)

# Modbus
%.o : %.cpp $(DEPDIR)/$(notdir %.d)
	${CXX} ${DEPFLAGS} -c $< -o $@ ${CFLAGS}
	$(POSTCOMPILE)

test/%.o : test/%.cpp $(DEPDIR)/$(notdir %.d)
	${CXX} ${DEPFLAGS} -c $< -o $@ ${CFLAGS}
	$(POSTCOMPILE)

$(SERIAL_BIN) : main.o $(SERIAL_OBJS)
	${CXX} $^ ${LDFLAGS} -o $@ $(SERIAL_LIBS)

$(TEST_DIR)/$(TEST_BIN): $(SERIAL_OBJS) $(TEST_OBJS)
	${CXX} $^ ${LDFLAGS} -o $@ $(TEST_LIBS) $(SERIAL_LIBS)

test: $(TEST_DIR)/$(TEST_BIN)
	rm -f $(TEST_DIR)/*.dat.out
	if [ "$(shell arch)" = "armv7l" ]; then \
        $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || { $(TEST_DIR)/abt.sh show; exit 1; } \
    else \
		valgrind --error-exitcode=180 -q $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || \
		if [ $$? = 180 ]; then \
			echo "*** VALGRIND DETECTED ERRORS ***" 1>& 2; \
			exit 1; \
		else $(TEST_DIR)/abt.sh show; exit 1; fi; \
	fi

clean :
	-rm -rf *.o $(SERIAL_BIN) $(DEPDIR)
	-rm -f $(TEST_DIR)/*.o $(TEST_DIR)/$(TEST_BIN)


install: all
	install -d $(DESTDIR)/usr/share/wb-mqtt-confed/schemas
	install -d $(DESTDIR)/var/lib/wb-mqtt-serial

	install -D -m 0644  data/config.sample.json $(DESTDIR)/etc/wb-mqtt-serial.conf.sample

	install -D -m 0644  data/config.json.wb234 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb234
	install -D -m 0644  data/config.json.wb5 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb5
	install -D -m 0644  data/config.json.wb6 $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.wb5
	install -D -m 0644  data/config.json.default $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.conf.default

	install -D -m 0644  wb-mqtt-serial.wbconfigs $(DESTDIR)/etc/wb-configs.d/11wb-mqtt-serial

	install -D -m 0644  data/wb-mqtt-serial.schema.json $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial.schema.json
	install -D -m 0644  data/wb-mqtt-serial-device-template.schema.json $(DESTDIR)/usr/share/wb-mqtt-serial/wb-mqtt-serial-device-template.schema.json
	cp -r  data/wb-mqtt-serial-templates $(DESTDIR)/usr/share/wb-mqtt-serial/templates

	install -m 0755  $(SERIAL_BIN) $(DESTDIR)/usr/bin/$(SERIAL_BIN)

$(DEPDIR)/$(notdir %.d): ;

-include $(patsubst %,$(DEPDIR)/%.d,$(notdir $(basename $(SRCS))))
