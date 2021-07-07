#ifndef ButtonHook_h
#define ButtonHook_h

struct Button {
   int pin;
   int previousState;
   int currentState;
   unsigned long pressStart;
   unsigned long pressRelease;
};

class ButtonHook {
 private:
   Button* buttons;

 public:
   ButtonHook();
   void checkButtons();
   void registerButton(int buttonPin);
};

#endif