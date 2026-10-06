"""Independent scalar integer mathematics for the frozen V1 contract."""

def clamp16(value):
    return max(-32768, min(32767, value))

def check_signed(value, width):
    if not -(1 << (width - 1)) <= value < 1 << (width - 1):
        raise ArithmeticError(f'signed {width}-bit range exceeded')
    return value

def wrap(value, width):
    code = value & ((1 << width) - 1)
    return code - (1 << width) if code & (1 << (width - 1)) else code

def high_mul(a, multiplier):
    check_signed(a, 32)
    check_signed(multiplier, 32)
    product = check_signed(a * multiplier, 64)
    if a == multiplier == -(1 << 31):
        return (1 << 31) - 1
    correction = 1 << 30
    if product < 0:
        numerator = product + 1 - correction
        return -((-numerator) // (1 << 31))
    return (product + correction) // (1 << 31)

def rdpot(value, shift):
    if shift == 0:
        return value
    magnitude = abs(value)
    quotient, remainder = divmod(magnitude, 1 << shift)
    if remainder * 2 >= 1 << shift:
        quotient += 1
    return -quotient if value < 0 else quotient

def mean(total, multiplier, exponent):
    check_signed(total, 32)
    scaled = check_signed(total * (1 << max(0, exponent)), 32)
    high = high_mul(scaled, multiplier)
    return clamp16(rdpot(high, max(0, -exponent)))

def rounded_shift(value, shift, mode=0, dbl_rnd=0):
    if not 0 <= shift <= 63 or not 0 <= dbl_rnd <= 30:
        raise ArithmeticError('invalid frozen shift')
    if shift == 0:
        return value
    offset = 1 << (shift - 1)
    if mode == 0:
        if shift + dbl_rnd > 31:
            delta = 1 << (30 - dbl_rnd)
            offset += delta if value >= 0 else -delta
    elif mode != 1:
        raise ArithmeticError('unfrozen rounding mode')
    return (value + offset) >> shift

def mul(a, b, multiplier, shift):
    product = check_signed(a * b, 32)
    widened = check_signed(product * multiplier, 64)
    return clamp16(rounded_shift(widened, shift))

def add(a, b, parameters):
    p1, p2, po = parameters['input1'], parameters['input2'], parameters['output']
    s1 = rounded_shift(a * p1['multiplier'], p1['right_shift'], 0, 15)
    s2 = rounded_shift(b * p2['multiplier'], p2['right_shift'], 0, 15)
    acc = check_signed(s1 + s2, 32)
    return clamp16(rounded_shift(check_signed(acc * po['multiplier'], 64), po['right_shift']))

def conv_rescale(accumulator, multiplier, shift):
    check_signed(accumulator, 48)
    return clamp16(rounded_shift(check_signed(accumulator * multiplier, 64), shift, 1))

def sigmoid_lut(value, table):
    z = min(126975, max(-126976, value))
    cell, fraction = divmod(z, 512)
    base, slope = table[cell + 256]
    adjustment = 16777728 if z >= 0 else 16777727
    return (base * 512 + fraction * slope + adjustment) // 1024

def logistic(value, multiplier, shift, table):
    return sigmoid_lut(rounded_shift(value * multiplier, shift), table)

def rational_rne(numerator, denominator):
    if denominator <= 0:
        raise ArithmeticError('invalid rational denominator')
    quotient, remainder = divmod(numerator, denominator)
    doubled = 2 * remainder
    if doubled > denominator or (doubled == denominator and quotient % 2):
        quotient += 1
    return clamp16(quotient)

def binary32_rational(bits):
    sign = -1 if bits >> 31 else 1
    exponent = (bits >> 23) & 255
    fraction = bits & 0x7fffff
    if exponent == 255:
        raise ArithmeticError('nonfinite bridge value')
    mantissa, power = (fraction, -149) if exponent == 0 else (fraction + (1 << 23), exponent - 150)
    if power >= 0:
        return sign * (mantissa << power), 1
    return sign * mantissa, 1 << -power

def binary32_bridge(value_bits, scale_bits):
    vn, vd = binary32_rational(value_bits)
    sn, sd = binary32_rational(scale_bits)
    if sn <= 0:
        raise ArithmeticError('nonpositive bridge scale')
    return rational_rne(vn * sd, vd * sn)

def coordinates(flat, shape):
    result = [0] * len(shape)
    for k in range(len(shape) - 1, -1, -1):
        flat, result[k] = divmod(flat, shape[k])
    return result

def flat_index(coords, shape):
    index = 0
    for c, size in zip(coords, shape):
        index = index * size + c
    return index

def transpose(shape, permutation, values):
    target_shape = [shape[k] for k in permutation]
    output = []
    for k in range(len(values)):
        source = [0] * len(shape)
        for axis, coordinate in zip(permutation, coordinates(k, target_shape)):
            source[axis] = coordinate
        output.append(values[flat_index(source, shape)])
    return output

def pad(shape, padding, values):
    target_shape = [size + padding[2*k] + padding[2*k+1] for k, size in enumerate(shape)]
    count = 1
    for size in target_shape:
        count *= size
    output = []
    for k in range(count):
        source = [c-padding[2*j] for j, c in enumerate(coordinates(k, target_shape))]
        valid = all(0 <= c < size for c, size in zip(source, shape))
        output.append(values[flat_index(source, shape)] if valid else 0)
    return output

def broadcast(shape, input_shape, values):
    count = 1
    for size in shape:
        count *= size
    output = []
    for k in range(count):
        coords = coordinates(k, shape)
        source = [0 if size == 1 else coords[len(shape)-len(input_shape)+j] for j, size in enumerate(input_shape)]
        output.append(values[flat_index(source, input_shape)])
    return output

def evaluate_case(case, table):
    a = case['arguments']
    p = case['primitive']
    if p == 'high_mul': return high_mul(a['a'], a['multiplier'])
    if p == 'rdpot': return rdpot(a['value'], a['shift'])
    if p == 'clamp16': return clamp16(a['value'])
    if p == 'wrap': return wrap(a['value'], a['width'])
    if p == 'mean': return mean(a['sum'], a['multiplier'], a['exponent'])
    if p == 'rounded_shift': return rounded_shift(a['value'], a['shift'], a['mode'], a['dbl_rnd'])
    if p == 'mul': return mul(a['a'], a['b'], a['multiplier'], a['shift'])
    if p == 'add': return add(a['a'], a['b'], a['parameters'])
    if p == 'conv_rescale': return conv_rescale(a['accumulator'], a['multiplier'], a['shift'])
    if p == 'sigmoid_lut': return sigmoid_lut(a['value'], table)
    if p == 'logistic': return logistic(a['value'], a['multiplier'], a['shift'], table)
    if p == 'dot': return sum(x*w for x, w in zip(a['inputs'], a['weights'])) + a['bias']
    if p == 'rational_rne': return rational_rne(a['numerator'], a['denominator'])
    if p == 'transpose': return transpose(a['shape'], a['permutation'], a['values'])
    if p == 'pad': return pad(a['shape'], a['padding'], a['values'])
    if p == 'reshape': return a['values'][:]
    if p == 'broadcast': return broadcast(a['shape'], a['input_shape'], a['values'])
    raise ArithmeticError('unimplemented frozen primitive ' + p)
