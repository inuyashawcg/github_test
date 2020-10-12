#include <iostream>
#include <cstdlib>
#include "genQueue.h"

using namespace std;

int option(int percents[]) {
    register int i = 0, choice = rand() % 100 + 1, perc;
    for (perc = percents[0]; perc < choice; perc += percents[i + 1], i++);
    return i;
}

int main() {
    int arrivals[] = {15, 20, 25, 10, 30};
    int service[] = {0, 0, 0, 10, 5, 10, 10, 0, 15, 25, 10, 15};
    int clerks[] = {0, 0, 0, 0}, numOfClerks = sizeof(clerks) / sizeof(int);
    int customers, t, i, numOfMinutes = 100, x;
    double maxWait = 0.0, currWait = 0.0, thereIsLine = 0.0;
    Queue<int> simulQ;
    
}