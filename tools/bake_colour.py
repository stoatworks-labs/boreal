#!/usr/bin/env python3
"""Print the CIE 1931 2-degree CMFs and CIE 1951 V-prime at the line wavelengths,
from the CVRL 1 nm tables (ciexyz31_1.csv and scvle_1.csv from cvrl.org, in the
working directory). The output is what Optics.cpp carries."""
import csv
def load(fn, cols):
    d={}
    for row in csv.reader(open(fn)):
        if not row or not row[0].strip(): continue
        d[int(float(row[0]))]=[float(x) for x in row[1:1+cols]]
    return d
xyz=load("ciexyz31_1.csv",3); sc=load("scvle_1.csv",1)
def interp(d,l):
    a=int(l); f=l-a
    return [ (1-f)*u+f*v for u,v in zip(d[a],d[a+1]) ]
lines=[("k5577",557.7339),("k6300",630.0304),("k6364",636.3776),("k4278",427.81),("k3914",391.44),
       ("k1P_6545",654.5),("k1P_6624",662.4),("k1P_6705",670.5),("k1P_6789",678.9)]
for n,l in lines:
    x,y,z=interp(xyz,l); v=interp(sc,l)[0] if int(l)+1 in sc and int(l) in sc else 0.0
    print(f"{n:10s} {l:9.4f}  xbar {x:.6f} ybar {y:.6f} zbar {z:.6f}  V' {v:.6f}  x={x/(x+y+z):.5f} y={y/(x+y+z):.5f}")
