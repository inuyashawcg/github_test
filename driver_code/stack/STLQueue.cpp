#include <iostream>
#include <queue>
#include <list>
using namespace std;

int main()
{
    queue<int> q1;
    q1.push(1);
    q1.push(2);
    q1.push(6);
    cout << "q1.back(): " << q1.back() << endl;
    cout << "Size of q1: " << q1.size();
    return 0;
}