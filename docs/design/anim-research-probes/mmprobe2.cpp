// Follow-up probe: does the early-out win or lose, and does the ANSWER DEPEND ON THE QUERY?
// ADR-182: the correctness arm plants a known row and both searches must find it.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <algorithm>
#include <cmath>
using Clock = std::chrono::steady_clock;

static int plain(const float* db, int n, int d, const float* q) {
    int best=-1; float bc=1e30f;
    for (int i=0;i<n;++i){ const float* r=db+(size_t)i*d; float c=0;
        for(int k=0;k<d;++k){float e=r[k]-q[k];c+=e*e;}
        if(c<bc){bc=c;best=i;} }
    return best;
}
static int early(const float* db, int n, int d, const float* q) {
    int best=-1; float bc=1e30f;
    for (int i=0;i<n;++i){ const float* r=db+(size_t)i*d; float c=0; int k=0;
        for(;k<d;++k){float e=r[k]-q[k];c+=e*e; if(c>=bc) break;}
        if(k==d&&c<bc){bc=c;best=i;} }
    return best;
}
int main(int argc,char**argv){
    const int n = argc>1?atoi(argv[1]):53500, d = argc>2?atoi(argv[2]):27, reps=200;
    std::mt19937 rng(9); std::normal_distribution<float> g(0,1);
    // Two databases: white noise (no temporal structure) and a random WALK (motion features are
    // a smooth trajectory through feature space, which is the property the AABB hierarchy exploits).
    std::vector<float> white((size_t)n*d), walk((size_t)n*d);
    for(auto&v:white)v=g(rng);
    { std::vector<float> cur(d,0.f);
      for(int i=0;i<n;++i){ for(int k=0;k<d;++k){cur[k]+=0.06f*g(rng); cur[k]*=0.999f; walk[(size_t)i*d+k]=cur[k];} } }
    const char* names[2]={"white noise","random walk (motion-like)"};
    std::vector<float>* dbs[2]={&white,&walk};
    for(int dbi=0;dbi<2;++dbi){
        float* db=dbs[dbi]->data();
        for(int mode=0;mode<2;++mode){ // 0 = far/random query, 1 = near query (a real row + noise)
            // correctness arm
            { std::vector<float> q(d); int planted=n/3;
              for(int k=0;k<d;++k) q[k]=db[(size_t)planted*d+k];
              if(plain(db,n,d,q.data())!=planted||early(db,n,d,q.data())!=planted){printf("PROBE BROKEN\n");return 1;} }
            double bp=1e30,be=1e30;
            std::vector<float> q(d);
            for(int r=0;r<reps;++r){
                if(mode==0){ for(auto&v:q)v=g(rng); }
                else { int row=(int)(rng()%(unsigned)n); for(int k=0;k<d;++k) q[k]=db[(size_t)row*d+k]+0.35f*g(rng); }
                auto t0=Clock::now(); volatile int a=plain(db,n,d,q.data());
                auto t1=Clock::now(); volatile int b=early(db,n,d,q.data());
                auto t2=Clock::now(); (void)a;(void)b;
                bp=std::min(bp,std::chrono::duration<double,std::micro>(t1-t0).count());
                be=std::min(be,std::chrono::duration<double,std::micro>(t2-t1).count());
            }
            printf("%-26s %-22s plain %8.1f us   early-out %8.1f us   ratio %.2fx\n",
                   names[dbi], mode?"near query":"far/random query", bp, be, be/bp);
        }
    }
    return 0;
}
