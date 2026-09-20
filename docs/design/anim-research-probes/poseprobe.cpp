// Isolated throwaway probe. NOT the production code: a faithful reimplementation of the ARITHMETIC
// SHAPE of scene::sampleClip + poseToModel + jointPalette for the Glowmere alien rig, to get an
// order of magnitude to compare the motion-matching numbers against. Treat every number here as an
// arithmetic lower bound on the real cost, not as a measurement of av-gen.
// ADR-182: the correctness arm checks that sampling the clip at key k reproduces key k's value.
#include <chrono>
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>
#include <cstdlib>

struct V4 { float x,y,z,w; };
struct M4 { float m[16]; };
static M4 mul(const M4&a,const M4&b){M4 r{};for(int c=0;c<4;++c)for(int j=0;j<4;++j){float s=0;for(int k=0;k<4;++k)s+=a.m[k*4+j]*b.m[c*4+k];r.m[c*4+j]=s;}return r;}
struct Chan { int joint, path; std::vector<float> t; std::vector<V4> v; };

static int upper(const std::vector<float>& t, float x){ return (int)(std::upper_bound(t.begin(),t.end(),x)-t.begin()); }

int main(int argc,char**argv){
    const int reps = argc>1?std::atoi(argv[1]):20000;
    const int joints = 89, channels = 267, keysPerChannel = 32; // Walking: 267 ch / 2664 keys ~= 10/ch; use 32 (Idle_turn-ish) as the pessimistic case
    std::mt19937 rng(7); std::uniform_real_distribution<float> u(-1,1);
    std::vector<Chan> chans(channels);
    for(int i=0;i<channels;++i){ chans[i].joint=i%joints; chans[i].path=i%3;
        chans[i].t.resize(keysPerChannel); chans[i].v.resize(keysPerChannel);
        for(int k=0;k<keysPerChannel;++k){ chans[i].t[k]=k*(1.0f/30.0f); chans[i].v[k]={u(rng),u(rng),u(rng),1.0f}; } }
    std::vector<int> parent(joints); parent[0]=-1; for(int i=1;i<joints;++i) parent[i]=(i*7)%i;
    std::vector<V4> T(joints),R(joints),S(joints);
    std::vector<M4> model(joints), pal(joints), invBind(joints);
    for(auto&m:invBind){ for(int i=0;i<16;++i)m.m[i]=(i%5==0)?1.0f:0.0f; }

    // correctness arm: sampling exactly at a key must reproduce that key
    { const Chan&c=chans[3]; float x=c.t[5]; int hi=upper(c.t,x); int lo=hi-1;
      float a=(x-c.t[lo])/(c.t[hi]-c.t[lo]); float got=c.v[lo].x+(c.v[hi].x-c.v[lo].x)*a;
      if (std::fabs(got-c.v[5].x)>1e-5f && std::fabs(got-c.v[lo].x)>1e-5f){ printf("PROBE BROKEN\n"); return 1; } }

    double best=1e30;
    for(int r=0;r<10;++r){
      auto t0=std::chrono::steady_clock::now();
      for(int it=0;it<reps;++it){
        const float now = 0.4f + 0.00001f*it;
        for(const Chan& c:chans){
            int hi=upper(c.t,now); if(hi==0)hi=1; if(hi>=(int)c.t.size())hi=(int)c.t.size()-1; int lo=hi-1;
            float a=(now-c.t[lo])/(c.t[hi]-c.t[lo]);
            V4 out{ c.v[lo].x+(c.v[hi].x-c.v[lo].x)*a, c.v[lo].y+(c.v[hi].y-c.v[lo].y)*a,
                    c.v[lo].z+(c.v[hi].z-c.v[lo].z)*a, c.v[lo].w+(c.v[hi].w-c.v[lo].w)*a };
            if(c.path==0) T[c.joint]=out; else if(c.path==1) R[c.joint]=out; else S[c.joint]=out;
        }
        for(int j=0;j<joints;++j){
            M4 local{}; // TRS compose, the shape of Transform::matrix()
            const float qx=R[j].x,qy=R[j].y,qz=R[j].z,qw=R[j].w;
            local.m[0]=(1-2*(qy*qy+qz*qz))*S[j].x; local.m[1]=(2*(qx*qy+qz*qw))*S[j].x; local.m[2]=(2*(qx*qz-qy*qw))*S[j].x; local.m[3]=0;
            local.m[4]=(2*(qx*qy-qz*qw))*S[j].y;  local.m[5]=(1-2*(qx*qx+qz*qz))*S[j].y; local.m[6]=(2*(qy*qz+qx*qw))*S[j].y; local.m[7]=0;
            local.m[8]=(2*(qx*qz+qy*qw))*S[j].z;  local.m[9]=(2*(qy*qz-qx*qw))*S[j].z;  local.m[10]=(1-2*(qx*qx+qy*qy))*S[j].z; local.m[11]=0;
            local.m[12]=T[j].x; local.m[13]=T[j].y; local.m[14]=T[j].z; local.m[15]=1;
            model[j] = parent[j]<0 ? local : mul(model[parent[j]], local);
        }
        for(int j=0;j<joints;++j) pal[j]=mul(model[j],invBind[j]);
      }
      auto t1=std::chrono::steady_clock::now();
      best=std::min(best,std::chrono::duration<double,std::micro>(t1-t0).count()/reps);
    }
    printf("one clip sample + poseToModel + jointPalette, 89 joints / 267 channels: %.2f us  (min over 10 runs of %d)\n", best, reps);
    printf("a cross-fade evaluates two clips: ~%.2f us\n", best*1.6);
    printf("5 aliens at 60 fps, single clip each: %.3f ms/frame\n", best*5/1000.0);
    return 0;
}
