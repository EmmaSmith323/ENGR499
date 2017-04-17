#include "lib/config.h"
#include "lib/scan.h"
#include "lib/rfcomm.h"
#include "lib/prompt.h"
#include "bluetooth.h"

#include <iostream>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <stdexcept>

using namespace std;

void print_menu();
void exchangeMsgs(deque<string>&, deque<string>&);

void runBluetoothSend(deque<string>&, string, Bluetooth&, int);
void runBluetoothReceive(deque<string>&, Bluetooth&, int);

mutex mtx;
condition_variable bt_send, bt_send2, main_cv;

int main(int argc, char **argv)
{

	if (argc < 1) {
		cerr << "Error, correct format: ./<executable name> $(./getMyBtAddr.sh)" << endl;
	}
	string myAddr(argv[1]);
	cout << "My addresss is " << myAddr << endl;

	deque<string> deviceBtAddrQ;
	deviceBtAddrQ.push_back("00:04:4B:66:9F:3A");
	deviceBtAddrQ.push_back("00:04:4B:65:BB:42");
	deviceBtAddrQ.push_back("00:00:00:00:00:00");

	// check your own address
	// do corresponding init work
	Bluetooth bluetooth(deviceBtAddrQ);
	bluetooth.setMyAddress(myAddr);

	cout << "My address index is " << bluetooth.getMyAddress() << endl;

	deque<string> send_msgs;
	deque<string> received_msgs;
	deque<string> bufferQ;

	vector<thread> sendThreads, receiveThreads;
	for (int i = 0, j = 1; (unsigned)i < deviceBtAddrQ.size(); i++) {
		if (i != bluetooth.getMyAddress()) {
			sendThreads.push_back(thread(runBluetoothSend, ref(send_msgs), deviceBtAddrQ[i],
				ref(bluetooth), j));
			receiveThreads.push_back(thread(runBluetoothReceive, ref(received_msgs),
				ref(bluetooth), j++));
		}
	}

	unique_lock<mutex> lck(mtx);
	string temp;
	while (true) {
		lck.unlock();
		cout << "> Say something: ";
		cin >> temp;
		cout << "try to send \"" << temp << "\"" << endl;

		printf("Main: wake up sending thread\n");

		lck.lock();
		send_msgs.push_back(temp);
		// wake up sending thread
		bt_send.notify_one();
		cout << "waked up!!" << endl;
		temp.clear();
		//main_cv.wait(lck);
	}


	for (int i = 0; (unsigned)i < sendThreads.size(); i++) {
		sendThreads[i].join();
	}
	for (int i = 0; (unsigned)i < receiveThreads.size(); i++) {
		receiveThreads[i].join();
	}

	return 0;
}

void runBluetoothSend(deque<string>& msgs, string dest,
	Bluetooth& bluetooth, int threadNum)
{
	condition_variable &bt_sendRef = threadNum == 1 ? bt_send : bt_send2;
	unique_lock<mutex> lck(mtx);
	lck.unlock();
	cout << "Thread: send begin" << endl;

	int index = bluetooth.find(dest);
	if (index > bluetooth.getMyAddress()) {
		// connect to lower index
		bt_sendRef.wait(lck);
	}
	cout << boolalpha << "connect to " << dest << "....\nSuccessful? "
		<< bluetooth.connect(index, threadNum) << endl;


	string msg;
	msg.push_back('0' + bluetooth.getMyAddress());
	if (bluetooth.send(index, msg) <= 0) {
		// notify the device
		cerr << "Failed: unable to send \"" << msg << "\""
			<< endl;
	}

	while (true) {
		printf("Thread: sending\n");
		lck.lock();
		printf("msgQ size = %d\n", (int)msgs.size());
		int size = msgs.size();
		for (int i = 0; i < size; i++) {
			int destIndex = msgs[0][0] - '0';
			if (bluetooth.getCurrTheradNum(destIndex) == threadNum) {
				// should send in this thread
				if (bluetooth.send(index, msgs[i]) <= 0) {
					cout << "Failed: unable to send \"" << msgs[i] << "\""
						<< endl;

					// break the sending loop
					break;
				}

				cout << "Msg sent: \"" << msgs[i] << "\"" << endl;
			}
			else {
				msgs.push_back(msgs[0]);
			}
			msgs.pop_front();
		}
		msgs.clear();
		/*lck.unlock();

		lck.lock();*/
		if (msgs.empty()) {
			cout << "Thread: acquire sending lock" << endl;
			bt_sendRef.wait(lck);
		}
		lck.unlock();
	}
}

void runBluetoothReceive(deque<string>& msgs, Bluetooth& bluetooth,
	int threadNum)
{
	unique_lock<mutex> lck(mtx);
	lck.unlock();
	cout << "pack_number" << threadNum << ": receive begin" << endl;

	if (bluetooth.initListener(threadNum) == false) {
		cerr << "invalid threadNum = " << threadNum << endl;
		return;
	}

	char buffer[1024];
	memset(buffer, '\0', 1024);

	string msg;
	while (bluetooth.listen(threadNum, msg, buffer) <= 0) {
		printf("Failed: unable to receive data\n");
	}

	int confirmedDeviceIndex = -1;
	try {
		confirmedDeviceIndex = stoi(msg);
	}
	catch (invalid_argument ex) {
		cerr << "confirmation message \"" << msg << "\" is invalid" << endl;
		return;
	}
	catch (out_of_range ex) {
		cerr << "confirmation message too large" << endl;
		return;
	}

	if (confirmedDeviceIndex < 0 || confirmedDeviceIndex >= 3)
		return;
	condition_variable *bt_sendRef = NULL;
	confirmedDeviceIndex = bluetooth.getMyAddress() - confirmedDeviceIndex;
	if (confirmedDeviceIndex > 0) {
		// myAddr = 3, index == 1: myAddr - index = 3 - 1 = 2, 2 % 2 = 0 -> bt_send
		// myAddr = 3, index == 2: myAddr - index = 3 - 2 = 1, 1 % 2 = 1 -> bt_send2
		// myAddr = 2, index == 1: myAddr - index = 2 - 1 = 1, 1 % 2 = 1 -> bt_send2
		if (confirmedDeviceIndex % 2 == 1) {
			bt_sendRef = &bt_send2;
		}
		else {
			bt_sendRef = &bt_send;
		}
	}
	if (bt_sendRef != NULL) {
		bt_sendRef->notify_one();
	}

	msg.clear();
	while (true) {
		printf("Thread: listening: \n");
		if (bluetooth.listen(threadNum, msg, buffer) <= 0) {
			printf("Failed: unable to receive data\n");
		}
		else {
			cout << buffer << endl;
			lck.lock();
			msgs.push_back(msg);
			lck.unlock();
		}
		// clear the string
		msg.clear();
	}
}


void exchangeMsgs(deque<string>& destQ, deque<string>& srcQ) {
	while (!srcQ.empty()) {
		destQ.push_back(srcQ[0]);
		srcQ.pop_front();
	}

}
