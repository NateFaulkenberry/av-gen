# Simulates params::ProcessorChain::process (src/params/processor.cpp:64-140) for a one-frame event
import json, math
FPS=60.0; dt=1/FPS
def peak_out(attack, decay, strength=1.0, frames=400):
    s=0.0; init=False; best=0.0
    for f in range(frames):
        x = strength if f==1 else 0.0
        y=x
        if not init: s=y; init=True
        else:
            tau = attack if y> s else decay
            c = 1.0 if tau<=0 else 1-math.exp(-dt/(tau/1000.0))
            s = y if c>=1 else s+(y-s)*c
        best=max(best,s)
    return best
d=json.load(open('/Users/natefaulkenberry/Documents/GitHub/av-gen-gv3/examples/world/glowmere-valley-2-multicam.json'))
events={'music.beat','music.impact','music.downbeat','music.phrase','music.section','music.build','music.break','music.drop','beat.pulse','audio.onset'}
for i,r in enumerate(d['routes']):
    if r['source'] in events:
        ch=r['chain']; p=peak_out(ch.get('attackMs',0),ch.get('decayMs',0))
        print(f"{i:2d} {r['source']:14s} -> {r['target']:55s} amt {r['amount']:7.3f} attack {ch.get('attackMs',0):6.0f}ms  peak-fraction {p:.3f}  peak-delta {p*r['amount']:.4f}")
