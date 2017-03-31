#include <iostream>
#include <string>

class PrinterBase {
public:
  virtual ~PrinterBase(){}

  virtual void print(char const *) =0;
};

class PrinterA : public PrinterBase {
public:
  ~PrinterA()  {}

  void print(char const * msg) override {
    std::cout << "A: " << msg << std::endl;
  }
};

class PrinterB : public PrinterBase {
public:
  ~PrinterB() {}

  void print(char const * msg) override {
    std::cout << "B: " << msg << std::endl;
  }
};

int main(int argc, char** argv) {
  PrinterBase* pa = new PrinterA();
  PrinterBase* pb = new PrinterB();

  std::string line;
  while (std::getline(std::cin, line))
  {
    std::cout << line << std::endl;
    if (line.at(0) == 'a') {
      pa->print(line.c_str());
    } else if (line.at(0) == 'b') {
      pb->print(line.c_str());
    }
  }
  delete pa;
  delete pb;
  return 0;
}