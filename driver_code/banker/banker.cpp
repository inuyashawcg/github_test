#include <iostream>

using namespace std;

const int process_num = 4;
const int resource_num = 3;

int source_req[resource_num] = {1, 0, 1}; 

int resource[resource_num] = {9, 3, 6};
int available[process_num] = {1, 1, 2};
int claim[process_num][resource_num] = {3, 2, 2, 6, 1, 3,
                                        3, 1, 4, 4, 2, 2};

int alloction[process_num][resource_num] = {1, 0, 0, 5, 1, 1,
                                            2, 1, 1, 0, 0, 2};


bool isSafe(int process_id, int add[3]) {
    if (process_id > 0 && process_id < process_num) {
        for (int i = 0; i < resource_num; i++) {
            if (add[i] <= available[i]) {
                alloction[process_id - 1][i] = add[i];
                available[i] -= add[i];
            } else {
                cout << "Resource is not avaliable!" << endl;
                return false;
            }
        }
    } else if (0 == process_id) {
        
    } else {
        cout << "Parameter error!" << endl;
        return false;
    }

    int processedNum = 0, tempPrcNum = 0;
    bool processFlag[process_num] = {false, false, false, false};
    bool possible = true;

    while(true) {
        for (int i = 0; i < process_num; i++) {
            if (!processFlag[i]) {
                for (int j = 0; j < resource_num; j++) {
                    if (claim[i][j] <= resource[j] && (claim[i][j] - alloction[i][j] >= 0)) {
                        if (claim[i][j] - alloction[i][j] <= available[j]) {
                            continue;
                        } else {
                            possible = false;
                            break;
                        }

                    } else { 
                        cout << "Resource allocation error!" << endl;
                        return false;
                    }
                } //

                if (possible) {
                    for (int j = 0; j < resource_num; j++) {
                        available[j] += alloction[i][j];
                        processFlag[i] = true;
                        ++processedNum;

                        if (4 == processedNum) {
                            cout << "This state is safe!" << endl;
                            return true;                        }
                    }
                } else {
                    possible = true;
                }
            } 
        }

        if (tempPrcNum != processedNum) {
            tempPrcNum = processedNum;
            continue;
        } else {
            cout << tempPrcNum << '\t' << processedNum << endl;
            cout << "This state is not safe!" << endl;
            return false;
        }
    }
}

int main() {
    bool safeFlag = isSafe(1, source_req);
    cout << safeFlag << endl;
    return 0;
}