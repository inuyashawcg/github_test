#include <iostream>
#include <queue>
#include <functional>
#include <string.h>

using namespace std;

class person {
public:
    person(const char *n = "", int a = 0) {
        name = (char*)malloc(strlen(n) + 1);
        strcpy(name, n);
        age = a;
    }

    ~person() {
        cout << "free" << endl;
        free(name);
    }

    person(const person& per) {
        name = (char*)malloc(strlen(per.name)+1);
        age = per.age;
        strcpy(name, per.name);
        cout << name << endl;
    }
      
private:
    char* name;
    int age;
};

int main() {
    person* p[] = {new person("Gregg", 25), new person("Ann", 30), new person("Bill", 20)};
    priority_queue<const person* > pqName1(p, p + 3);
    //priority_queue<person, vector<person>, greater<person>> pqName2(p, p+3);
    //priority_queue<person, vector<person>, lesserAge> pqAge(p, p+3);

    // while(!pqName1.empty()) {
    //     cout << pqName1.top().getName() << ' ';
    //     pqName1.pop();
    // }
    cout << '\n';

    return 0;
}