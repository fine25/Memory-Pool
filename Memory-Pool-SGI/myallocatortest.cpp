#include "myallocator.h"
#include <vector>
using namespace std;

int main()
{
	
	vector < int, SGIAllocator<int> > vec;
	for (int i = 0; i < 1000; i++)
	{
		int data = rand() % 100;
		vec.push_back(data);
	}
	for (int val : vec)
	{
		cout << val << endl;
	}
	
	return 0;

}