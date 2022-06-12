#include <iostream>
#include "player.h"

using namespace std;
int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		cout << "Please provide a movie file, usage: " << endl;
		return -1;
	}
	//cout << "Try playing " << argv[1] << "..."<<endl;
	PlayerRunning(argv[1]);
	return 0;
}
