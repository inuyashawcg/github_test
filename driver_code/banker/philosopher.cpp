#include <iostream>
#include <thread>
#include <chrono>

using namespace std;

void pause_thread(int n) {
    this_thread::sleep_for(chrono::seconds(n));
    cout << "pause of " << n << "seconds ended" << endl;
}

int main() {
    thread threads[5];

    cout << "Spawning 5 threads" << endl;

    for (int i = 0; i < 5; i++) {
        threads[i] = thread(pause_thread, i + 1);
        cout << "Done spawning threads. Now waiting for them to join..." << endl;
    }

    for (int i = 0; i < 5; i++) {
        threads[i].join();
    }

    cout << "All threads joined" << endl;
    return 0;  
}