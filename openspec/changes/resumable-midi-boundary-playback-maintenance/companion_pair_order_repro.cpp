#include <algorithm>
#include <iostream>
#include <numeric>
#include <vector>
struct E { int tick; bool on; };
int pair(const std::vector<E>& e, const std::vector<int>& order, int target) {
 std::vector<int> stack;
 for(int i:order) { if(e[i].on) stack.push_back(i); else if(!stack.empty()) {int on=stack.back();stack.pop_back();if(on==target)return i;}}
 return -1;
}
int main() {
 for(int n=3;n<=7;++n) {int max=1;for(int i=0;i<n;++i)max*=6;
 for(int code=0;code<max;++code) {int v=code;std::vector<E> e;int target=-1;
 for(int i=0;i<n;++i) {int x=v%6;v/=6;e.push_back({(x/2)*96,bool(x%2)});if(e.back().on && target<0)target=i;}
 if(target<0)continue;
 std::vector<int>a(n),b(n),tmp(n);std::iota(a.begin(),a.end(),0);b=a;
 auto less=[&](int x,int y) {return e[x].tick==e[y].tick ? e[x].on<e[y].on : x<y;};
 std::stable_sort(a.begin(),a.end(),less);
 for(int w=1;w<n;w*=2) {for(int i=0;i<n;i+=2*w)std::merge(b.begin()+i,b.begin()+std::min(i+w,n),b.begin()+std::min(i+w,n),b.begin()+std::min(i+2*w,n),tmp.begin()+i,less);b=tmp;}
 int p=pair(e,a,target),q=pair(e,b,target);if(p>=0 && q>=0 && p!=q && e[p].tick!=e[q].tick) {
 std::cout<<"target "<<target<<"\nevents ";for(int i=0;i<n;++i)std::cout<<i<<":"<<(e[i].on?"On":"Off")<<"@"<<e[i].tick<<" ";
 std::cout<<"\nstd stable_sort ";for(int i:a)std::cout<<i<<" ";std::cout<<"pair "<<p<<"\nbounded merge ";for(int i:b)std::cout<<i<<" ";std::cout<<"pair "<<q<<"\n";return 0;}
 }}return 1;
}
