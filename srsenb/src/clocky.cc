#include <iostream>
#include <time.h>

int main(int argc, char* argv[]){
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    std::cout << "Current seconds: " << now.tv_sec << "\n";
    return 0;
}