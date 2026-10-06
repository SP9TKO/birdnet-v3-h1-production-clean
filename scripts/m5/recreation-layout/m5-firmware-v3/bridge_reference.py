"""Independent M5-3 reference: integer binary32 decoding and Fraction floor/RNE.
No production bridge imports or calls; no floating-point division or rounding.
Finite input and positive finite scale only, as frozen policy requires.
"""
from fractions import Fraction

def binary32(bits):
    sign=-1 if bits>>31 else 1
    exponent=(bits>>23)&255
    significand=bits&0x7fffff
    if exponent==255:raise ValueError('Nonfinite input is outside the frozen finite domain')
    if exponent:
        significand+=1<<23
        power=exponent-150
    else:power=-149
    if power>=0:return Fraction(sign*significand*(1<<power),1)
    return Fraction(sign*significand,1<<(-power))

def reference(source_bits,scale_bits):
    x=binary32(source_bits);s=binary32(scale_bits)
    if s<=0:raise ValueError('Frozen scale must be positive and finite')
    quotient=x/s
    lower=quotient.numerator//quotient.denominator
    fraction=quotient-lower
    if fraction>Fraction(1,2) or (fraction==Fraction(1,2) and lower%2):rounded=lower+1
    else:rounded=lower
    code=max(-32768,min(32767,rounded))
    saturation=-1 if rounded< -32768 else 1 if rounded>32767 else 0
    return quotient,code,saturation
