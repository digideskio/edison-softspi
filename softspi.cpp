#include "softspi.h"

Nan::Persistent<v8::Function> SoftSpi::constructor;

NAN_MODULE_INIT(SoftSpi::Init) {
  v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(SoftSpi::New);
  tpl->SetClassName(Nan::New("SoftSpi").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  Nan::SetPrototypeMethod(tpl, "write", write);
  Nan::SetPrototypeMethod(tpl, "frequency", frequency);
  Nan::SetPrototypeMethod(tpl, "mode", mode);
  Nan::SetPrototypeMethod(tpl, "betweenByteDelay_us", betweenByteDelay_us);

  constructor.Reset(Nan::GetFunction(tpl).ToLocalChecked());
  Nan::Set(target, Nan::New("SoftSpi").ToLocalChecked(), Nan::GetFunction(tpl).ToLocalChecked());
}

SoftSpi::SoftSpi(
  mraa_gpio_context sck,
  mraa_gpio_context mosi,
  mraa_gpio_context miso
) : m_sck(sck), m_mosi(mosi), m_miso(miso) {
  setMode(SoftSpi::DEFAULT_SPI_MODE);
  setFrequency(SoftSpi::DEFAULT_FREQUENCY);
  setBetweenByteDelay_us(SoftSpi::DEFAULT_BETWEEN_BYTE_DELAY_US);
}

SoftSpi::~SoftSpi() {
  mraa_gpio_close(m_sck);
  mraa_gpio_close(m_mosi);
  mraa_gpio_close(m_miso);
}

void SoftSpi::setFrequency(uint32_t frequency) {
  m_frequency = frequency;
  int32_t c = (1.0 / (double)frequency) * ((double)LOOPS_PER_SECOND) / 2.0;
  if(c < 0) {
    c = 1;
  }
  m_clockSleepCount = c;
}

// SPI Mode  CPOL  CPHA
//    0       0     1
//    1       0     0
//    2       1     1
//    3       1     0
void SoftSpi::setMode(SpiMode mode) {
  m_mode = mode;

  // CS may be asserted later before a call to write has a chance
  // to change the SCK.
  if (m_mode == 2 || m_mode == 3) {
    mraa_gpio_write(m_sck, 1);
    m_cpol = 1;
  } else {
    mraa_gpio_write(m_sck, 0);
    m_cpol = 0;
  }
}

void SoftSpi::setBetweenByteDelay_us(uint32_t t) {
  m_betweenByteSleepCount = ((double)t / 1000.0 / 1000.0) * (double)LOOPS_PER_SECOND;
}

void SoftSpi::write(uint8_t* buffer, uint32_t bufferLength) {
  for(uint32_t i = 0; i < bufferLength; i++) {
    uint8_t b = buffer[i];
    buffer[i] = writeByte(b);
    usleepByCounting(m_betweenByteSleepCount);
  }
}

uint8_t SoftSpi::writeByte(uint8_t byte) {
  uint8_t result = 0;
  for(int i = 0; i < 8; i++) {
    int bit = byte & 0x80;
    result = result << 1;
    result |= writeBit(bit) == 0 ? 0 : 1;
    byte = byte << 1;
  }
  return result;
}

uint8_t SoftSpi::writeBit(uint8_t bit) {
  if (m_mode == 0 || m_mode == 2) {
    // Mode 0: CPOL = 0, CPHA = 0, Clock idle low, data is clocked in on rising edge, output data (change) on falling edge
    // Mode 2: CPOL = 1, CPHA = 0, Clock idle low, data is clocked in on falling edge, output data (change) on rising edge
    mraa_gpio_write(m_mosi, (bit == 0) ? 0 : 1);
    usleepByCounting(m_clockSleepCount);
    uint8_t r = mraa_gpio_read(m_miso);
    mraa_gpio_write(m_sck, (m_cpol == 0) ? 1 : 0);
    usleepByCounting(m_clockSleepCount);
    mraa_gpio_write(m_sck, (m_cpol == 0) ? 0 : 1);
    return r;
  } else if (m_mode == 1 || m_mode == 3) {
    // Mode 1: CPOL = 0, CPHA = 1, Clock idle low, data is clocked in on falling edge, output data (change) on rising edge
    // Mode 3: CPOL = 1, CPHA = 1, Clock idle low, data is clocked in on rising, edge output data (change) on falling edge
    mraa_gpio_write(m_sck, (m_cpol == 0) ? 1 : 0);
    mraa_gpio_write(m_mosi, (bit == 0) ? 0 : 1);
    usleepByCounting(m_clockSleepCount);
    mraa_gpio_write(m_sck, (m_cpol == 0) ? 0 : 1);
    uint8_t r = mraa_gpio_read(m_miso);
    usleepByCounting(m_clockSleepCount);
    return r;
  } else {
    printf("unhandled spi mode %d\n", m_mode);
    return -1;
  }
}

void SoftSpi::usleepByCounting(uint32_t sleepCount) {
  volatile uint32_t i;
  for(i = 1; i < sleepCount; i++);
}

NAN_METHOD(SoftSpi::New) {
  v8::Local<v8::Object> options = info[0]->ToObject();
  uint32_t sckPin = options->Get(Nan::New("sck").ToLocalChecked())->ToUint32()->Value();
  uint32_t mosiPin = options->Get(Nan::New("mosi").ToLocalChecked())->ToUint32()->Value();
  uint32_t misoPin = options->Get(Nan::New("miso").ToLocalChecked())->ToUint32()->Value();

  mraa_gpio_context sck = mraa_gpio_init(sckPin);
  mraa_gpio_dir(sck, MRAA_GPIO_OUT);
  mraa_gpio_use_mmaped(sck, 1);

  mraa_gpio_context mosi = mraa_gpio_init(mosiPin);
  mraa_gpio_dir(mosi, MRAA_GPIO_OUT);
  mraa_gpio_use_mmaped(mosi, 1);

  mraa_gpio_context miso = mraa_gpio_init(misoPin);
  mraa_gpio_dir(miso, MRAA_GPIO_IN);
  mraa_gpio_use_mmaped(miso, 1);

  if (info.IsConstructCall()) {
    SoftSpi *obj = new SoftSpi(sck, mosi, miso);
    obj->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    const int argc = 1;
    v8::Local<v8::Value> argv[argc] = {info[0]};
    v8::Local<v8::Function> cons = Nan::New(constructor);
    info.GetReturnValue().Set(cons->NewInstance(argc, argv));
  }
}

NAN_METHOD(SoftSpi::frequency) {
  SoftSpi* softSpi = Nan::ObjectWrap::Unwrap<SoftSpi>(info.This());
  uint32_t frequency = info[0]->ToUint32()->Value();
  softSpi->setFrequency(frequency);
}

NAN_METHOD(SoftSpi::mode) {
  SoftSpi* softSpi = Nan::ObjectWrap::Unwrap<SoftSpi>(info.This());
  SpiMode mode = (SpiMode)info[0]->ToUint32()->Value();
  softSpi->setMode(mode);
}

NAN_METHOD(SoftSpi::betweenByteDelay_us) {
  SoftSpi* softSpi = Nan::ObjectWrap::Unwrap<SoftSpi>(info.This());
  uint32_t t = info[0]->ToUint32()->Value();
  softSpi->setBetweenByteDelay_us(t);
}

NAN_METHOD(SoftSpi::write) {
  SoftSpi* softSpi = Nan::ObjectWrap::Unwrap<SoftSpi>(info.This());
  unsigned char* buffer = (unsigned char*) node::Buffer::Data(info[0]->ToObject());
  uint32_t bufferLength = (uint32_t) node::Buffer::Length(info[0]->ToObject());
  softSpi->write(buffer, bufferLength);
}
