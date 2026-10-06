// Independent scalar implementation of the frozen V1 mathematical contract.
// Only standard C++ libraries are used; no model-runtime/kernel libraries.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using I = int64_t;
using Wide = __int128_t;

static I checked(Wide x, int bits) {
    Wide bound = Wide(1) << (bits - 1);
    if (x < -bound || x >= bound) throw std::runtime_error("signed arithmetic range exceeded");
    return I(x);
}
static I floor_div(Wide x, Wide d) {
    Wide q = x / d;
    if (x < 0 && x % d) --q;
    return checked(q, 64);
}
static I clip(I x) { return std::max<I>(-32768, std::min<I>(32767, x)); }
static I rounded(Wide x, int shift, int mode = 0, int dbl = 0) {
    if (shift < 0 || shift > 63 || dbl < 0 || dbl > 30) throw std::runtime_error("invalid shift");
    if (!shift) return checked(x, 64);
    Wide bias = Wide(1) << (shift - 1);
    if (mode == 0) {
        if (shift + dbl > 31) bias += (x >= 0 ? 1 : -1) * (Wide(1) << (30 - dbl));
    } else if (mode != 1) throw std::runtime_error("unfrozen rounding mode");
    return floor_div(x + bias, Wide(1) << shift);
}
static I high(I x, I m) {
    checked(x, 32); checked(m, 32);
    if (x == -(I(1) << 31) && m == x) return (I(1) << 31) - 1;
    I product = checked(Wide(x) * m, 64);
    Wide nudge = product < 0 ? 1 - (Wide(1) << 30) : Wide(1) << 30;
    return checked((Wide(product) + nudge) / (Wide(1) << 31), 32);
}
static I power_round(I x, int r) {
    if (!r) return x;
    Wide mag = x < 0 ? -Wide(x) : Wide(x);
    Wide denom = Wide(1) << r;
    Wide q = mag / denom;
    if (2 * (mag % denom) >= denom) ++q;
    return checked(x < 0 ? -q : q, 64);
}
static I mean_code(I sum, I multiplier, int exponent) {
    checked(sum, 32);
    I before = checked(Wide(sum) * (Wide(1) << std::max(0, exponent)), 32);
    return clip(power_round(high(before, multiplier), std::max(0, -exponent)));
}
static I wrap_code(I value, int bits) {
    uint64_t mask = (uint64_t(1) << bits) - 1;
    uint64_t x = uint64_t(value) & mask;
    return x & (uint64_t(1) << (bits - 1)) ? I(Wide(x) - (Wide(1) << bits)) : I(x);
}
static I mul_code(I a, I b, I m, int r) {
    I p = checked(Wide(a) * b, 32);
    return clip(rounded(checked(Wide(p) * m, 64), r));
}
static I add_code(I a, I b, const std::vector<I>& p) {
    I s1 = rounded(Wide(a) * p[0], int(p[1]), 0, 15);
    I s2 = rounded(Wide(b) * p[2], int(p[3]), 0, 15);
    I sum = checked(Wide(s1) + s2, 32);
    return clip(rounded(checked(Wide(sum) * p[4], 64), int(p[5])));
}
static I conv_code(I acc, I m, int r) {
    checked(acc, 48);
    return clip(rounded(checked(Wide(acc) * m, 64), r, 1));
}
using Lut = std::vector<std::pair<I, I>>;
static I sigmoid_code(I value, const Lut& table) {
    I z = std::max<I>(-126976, std::min<I>(126975, value));
    I cell = floor_div(z, 512);
    I remainder = z - cell * 512;
    auto pair = table.at(size_t(cell + 256));
    I offset = z < 0 ? 0x10001ff : 0x1000200;
    return floor_div(Wide(pair.first) * 512 + remainder * pair.second + offset, 1024);
}
static I rne_code(I n, I d) {
    if (d <= 0) throw std::runtime_error("invalid rational denominator");
    I q = floor_div(n, d);
    Wide rem = Wide(n) - Wide(q) * d;
    if (2 * rem > d || (2 * rem == d && q % 2)) ++q;
    return clip(q);
}
static size_t count(const std::vector<int>& shape) {
    size_t n = 1;
    for (int d : shape) { if (d <= 0) throw std::runtime_error("invalid dimension"); n *= size_t(d); }
    return n;
}
static std::vector<int> coords(size_t flat, const std::vector<int>& shape) {
    std::vector<int> c(shape.size());
    for (size_t k = shape.size(); k--;) { c[k] = int(flat % shape[k]); flat /= shape[k]; }
    return c;
}
static size_t index(const std::vector<int>& c, const std::vector<int>& shape) {
    size_t n = 0;
    for (size_t k = 0; k < shape.size(); ++k) n = n * shape[k] + c[k];
    return n;
}
static size_t broadcast_index(size_t flat, const std::vector<int>& shape, const std::vector<int>& inshape) {
    auto c = coords(flat, shape);
    size_t n = 0, offset = shape.size() - inshape.size();
    for (size_t k = 0; k < inshape.size(); ++k) n = n * inshape[k] + (inshape[k] == 1 ? 0 : c[offset+k]);
    return n;
}
static std::vector<I> transpose_values(const std::vector<int>& shape, const std::vector<int>& perm, const std::vector<I>& input) {
    std::vector<int> outshape;
    for (int axis : perm) outshape.push_back(shape.at(axis));
    std::vector<I> out(count(outshape));
    for (size_t k = 0; k < out.size(); ++k) {
        auto c = coords(k, outshape); std::vector<int> source(shape.size());
        for (size_t j = 0; j < perm.size(); ++j) source[perm[j]] = c[j];
        out[k] = input.at(index(source, shape));
    }
    return out;
}
static std::vector<I> pad_values(const std::vector<int>& shape, const std::vector<int>& widths, const std::vector<I>& input) {
    auto outshape = shape;
    for (size_t j = 0; j < shape.size(); ++j) outshape[j] += widths[2*j] + widths[2*j+1];
    std::vector<I> out(count(outshape), 0);
    for (size_t k = 0; k < out.size(); ++k) {
        auto c = coords(k, outshape); bool valid = true;
        for (size_t j = 0; j < c.size(); ++j) { c[j] -= widths[2*j]; valid &= c[j] >= 0 && c[j] < shape[j]; }
        if (valid) out[k] = input.at(index(c, shape));
    }
    return out;
}

struct Reader {
    std::ifstream stream;
    explicit Reader(const std::string& path) : stream(path, std::ios::binary) { if (!stream) throw std::runtime_error("cannot open package"); }
    uint64_t number(unsigned bytes) {
        uint64_t n = 0;
        for (unsigned k = 0; k < bytes; ++k) { int c = stream.get(); if (c < 0) throw std::runtime_error("truncated metadata package"); n |= uint64_t(c) << (8*k); }
        return n;
    }
    I integer() { uint64_t u = number(8); I v; std::memcpy(&v, &u, 8); return v; }
    std::vector<int> shape() { std::vector<int> s(number(4)); for (int& v : s) v = int(number(4)); return s; }
};
struct Tensor {
    int dtype;
    std::vector<int> shape;
    std::vector<uint8_t> constant;
    std::vector<int16_t> codes;
    I scalar(size_t k) const {
        int bytes = dtype == 9 ? 1 : dtype == 7 ? 2 : dtype == 2 ? 4 : dtype == 4 ? 8 : 0;
        if (!bytes || (k+1)*bytes > constant.size()) throw std::runtime_error("constant read outside frozen buffer");
        uint64_t value = 0;
        for (int j = 0; j < bytes; ++j) value |= uint64_t(constant[k*bytes+j]) << (8*j);
        if (bytes < 8 && value & (uint64_t(1) << (bytes*8-1))) return I(Wide(value) - (Wide(1) << (bytes*8)));
        I result; std::memcpy(&result, &value, 8); return result;
    }
    I code(size_t k) const { return codes.empty() ? scalar(k) : codes.at(k); }
};
enum Kind { TRANSPOSE = 1, PAD = 2, CONV = 3, LOGISTIC = 4, MUL = 5, ADD = 6, DEPTHWISE = 7, MEAN = 8, RESHAPE = 9, FC = 10 };
struct Op { int ix, kind; std::vector<int> inputs; int output; std::vector<I> parameters; };

static void execute(const Op& op, std::vector<Tensor>& ts, const Lut& lut) {
    Tensor& y = ts.at(op.output);
    const Tensor& x = ts.at(op.inputs[0]);
    size_t n = count(y.shape); y.codes.resize(n);
    const auto& p = op.parameters;
    if (op.kind == TRANSPOSE || op.kind == PAD) {
        const Tensor& args = ts.at(op.inputs[1]); std::vector<int> v;
        for (size_t k = 0; k < count(args.shape); ++k) v.push_back(int(args.scalar(k)));
        std::vector<I> source;
        for (size_t k = 0; k < count(x.shape); ++k) source.push_back(x.code(k));
        auto values = op.kind == TRANSPOSE ? transpose_values(x.shape, v, source) : pad_values(x.shape, v, source);
        if (values.size() != n) throw std::runtime_error("shape mismatch in data movement");
        for (size_t k = 0; k < n; ++k) y.codes[k] = int16_t(values[k]);
    } else if (op.kind == RESHAPE) {
        if (count(x.shape) != n) throw std::runtime_error("reshape changes size");
        for (size_t k = 0; k < n; ++k) y.codes[k] = int16_t(x.code(k));
    } else if (op.kind == MUL || op.kind == ADD) {
        const Tensor& z = ts.at(op.inputs[1]);
        for (size_t k = 0; k < n; ++k) {
            I a = x.code(broadcast_index(k, y.shape, x.shape));
            I b = z.code(broadcast_index(k, y.shape, z.shape));
            y.codes[k] = int16_t(op.kind == MUL ? mul_code(a,b,p[0],int(p[1])) : add_code(a,b,p));
        }
    } else if (op.kind == LOGISTIC) {
        for (size_t k = 0; k < n; ++k) y.codes[k] = int16_t(sigmoid_code(rounded(Wide(x.code(k))*p[0],int(p[1])),lut));
    } else if (op.kind == MEAN) {
        int channels = x.shape.at(3), spatial = x.shape.at(1) * x.shape.at(2);
        if (n != size_t(channels) || p[2] != spatial) throw std::runtime_error("mean cardinality mismatch");
        for (int c = 0; c < channels; ++c) {
            I acc = 0;
            for (int k = 0; k < spatial; ++k) acc += x.code(size_t(k)*channels+c);
            y.codes[c] = int16_t(mean_code(acc,p[0],int(p[1])));
        }
    } else if (op.kind == FC) {
        const Tensor& w = ts.at(op.inputs[1]); const Tensor& bias = ts.at(op.inputs[2]);
        int oc = w.shape.at(0), depth = w.shape.at(1);
        for (size_t row = 0; row < n / oc; ++row) for (int c = 0; c < oc; ++c) {
            I acc = bias.scalar(c);
            const int8_t* weights = reinterpret_cast<const int8_t*>(w.constant.data()) + size_t(c)*depth;
            for (int k = 0; k < depth; ++k) acc += I(x.code(row*depth+k)) * weights[k];
            y.codes[row*oc+c] = int16_t(conv_code(acc,p[2*c],int(p[2*c+1])));
        }
    } else if (op.kind == CONV || op.kind == DEPTHWISE) {
        const Tensor& w = ts.at(op.inputs[1]); const Tensor& bias = ts.at(op.inputs[2]);
        int ih=x.shape.at(1), iw=x.shape.at(2), ic=x.shape.at(3);
        int oh=y.shape.at(1), ow=y.shape.at(2), oc=y.shape.at(3);
        int kh=w.shape.at(1), kw=w.shape.at(2), sh=int(p[0]), sw=int(p[1]), dh=int(p[2]), dw=int(p[3]);
        int ph=0,pw=0;
        if (p[4] == 0) { ph=std::max(0,(oh-1)*sh+(kh-1)*dh+1-ih)/2; pw=std::max(0,(ow-1)*sw+(kw-1)*dw+1-iw)/2; }
        const int8_t* weights=reinterpret_cast<const int8_t*>(w.constant.data());
        for (int batch=0;batch<y.shape[0];++batch) for (int oy=0;oy<oh;++oy) for (int ox=0;ox<ow;++ox) for (int c=0;c<oc;++c) {
            I acc=bias.scalar(c);
            for (int ky=0;ky<kh;++ky) {
                int sy=oy*sh+ky*dh-ph; if (sy<0||sy>=ih) continue;
                for (int kx=0;kx<kw;++kx) {
                    int sx=ox*sw+kx*dw-pw; if (sx<0||sx>=iw) continue;
                    size_t ip=((size_t(batch)*ih+sy)*iw+sx)*ic;
                    if (op.kind == DEPTHWISE) {
                        int ci=c/int(p[5]);
                        acc+=I(x.codes.at(ip+ci))*weights[(size_t(ky)*kw+kx)*oc+c];
                    } else {
                        const int16_t* input=x.codes.data()+ip;
                        const int8_t* filter=weights+((size_t(c)*kh+ky)*kw+kx)*ic;
                        for (int ci=0;ci<ic;++ci) acc+=I(input[ci])*filter[ci];
                    }
                }
            }
            y.codes[((size_t(batch)*oh+oy)*ow+ox)*oc+c]=int16_t(conv_code(acc,p[6+2*c],int(p[7+2*c])));
        }
    } else throw std::runtime_error("unimplemented source operator");
}

static void write_codes(const std::filesystem::path& file, const std::vector<int16_t>& codes) {
    std::ofstream out(file,std::ios::binary);
    if (!out) throw std::runtime_error("cannot create trace");
    for (int16_t code : codes) { uint16_t u=uint16_t(code); out.put(char(u&255));out.put(char(u>>8)); }
    if (!out) throw std::runtime_error("trace write failure");
}
static int model_run(const std::string& package, const std::string& output) {
    Reader r(package);
    std::string magic(8,'\0');r.stream.read(magic.data(),8);
    if (magic != std::string("ORCLV1\0\0",8)) throw std::runtime_error("package identity mismatch");
    std::vector<Tensor> ts(r.number(4));
    for (auto& t:ts) {
        t.dtype=int(r.number(4)); t.shape=r.shape(); t.constant.resize(r.number(8));
        r.stream.read(reinterpret_cast<char*>(t.constant.data()),std::streamsize(t.constant.size()));
        if (!r.stream) throw std::runtime_error("truncated constant");
    }
    Lut lut(r.number(4));for (auto& pair:lut) {pair.first=r.integer();pair.second=r.integer();}
    std::vector<Op> ops(r.number(4));
    for (auto& op:ops) {
        op.ix=int(r.number(4));op.kind=int(r.number(4));op.inputs=r.shape();op.output=int(r.number(4));
        op.parameters.resize(r.number(4));for (I& p:op.parameters)p=r.integer();
    }
    int input_id=int(r.number(4));auto& input=ts.at(input_id);input.codes.resize(r.number(8));
    for (auto& v:input.codes)v=int16_t(r.number(2));
    if (input.codes.size()!=count(input.shape)||r.stream.peek()!=std::ifstream::traits_type::eof()) throw std::runtime_error("input/package length mismatch");
    std::filesystem::create_directories(output);
    for (const auto& op:ops) {
        execute(op,ts,lut);
        std::ostringstream name;name<<"op"<<op.ix<<"-tensor"<<op.output<<".i16le";
        write_codes(std::filesystem::path(output)/name.str(),ts.at(op.output).codes);
        if ((op.ix+1)%32==0 || size_t(op.ix+1)==ops.size()) std::cout<<"executed "<<op.ix+1<<"/"<<ops.size()<<std::endl;
    }
    return 0;
}

static std::vector<int> ints(std::istringstream& s, size_t n) { std::vector<int> v(n);for (int& x:v)s>>x;return v; }
static std::vector<I> values(std::istringstream& s,size_t n) { std::vector<I> v(n);for (I& x:v)s>>x;return v; }
static int primitive_run(const std::string& lut_path) {
    std::ifstream file(lut_path);Lut lut(512);for(auto& p:lut)file>>p.first>>p.second;
    if(!file)throw std::runtime_error("LUT metadata input failure");
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream s(line);std::string id,primitive;s>>id>>primitive;std::vector<I> out;
        I a,b,m;int shift,mode,dbl;
        if(primitive=="high_mul"){s>>a>>m;out={high(a,m)};}
        else if(primitive=="rdpot"){s>>a>>shift;out={power_round(a,shift)};}
        else if(primitive=="clamp16"){s>>a;out={clip(a)};}
        else if(primitive=="wrap"){s>>a>>shift;out={wrap_code(a,shift)};}
        else if(primitive=="mean"){s>>a>>m>>shift;out={mean_code(a,m,shift)};}
        else if(primitive=="rounded_shift"){s>>a>>shift>>mode>>dbl;out={rounded(a,shift,mode,dbl)};}
        else if(primitive=="mul"){s>>a>>b>>m>>shift;out={mul_code(a,b,m,shift)};}
        else if(primitive=="add"){s>>a>>b;auto p=values(s,6);out={add_code(a,b,p)};}
        else if(primitive=="conv_rescale"){s>>a>>m>>shift;out={conv_code(a,m,shift)};}
        else if(primitive=="sigmoid_lut"){s>>a;out={sigmoid_code(a,lut)};}
        else if(primitive=="logistic"){s>>a>>m>>shift;out={sigmoid_code(rounded(Wide(a)*m,shift),lut)};}
        else if(primitive=="rational_rne"){s>>a>>b;out={rne_code(a,b)};}
        else if(primitive=="dot"){size_t n;s>>n;auto x=values(s,n);auto w=values(s,n);s>>b;Wide sum=b;for(size_t k=0;k<n;++k)sum+=Wide(x[k])*w[k];out={checked(sum,48)};}
        else if(primitive=="transpose"||primitive=="pad"){size_t rank,n;s>>rank;auto shape=ints(s,rank);auto args=ints(s,primitive=="pad"?rank*2:rank);s>>n;auto x=values(s,n);out=primitive=="pad"?pad_values(shape,args,x):transpose_values(shape,args,x);}
        else if(primitive=="reshape"){size_t n;s>>n;out=values(s,n);}
        else if(primitive=="broadcast"){size_t rank,inrank,n;s>>rank;auto shape=ints(s,rank);s>>inrank;auto inshape=ints(s,inrank);s>>n;auto x=values(s,n);for(size_t k=0;k<count(shape);++k)out.push_back(x.at(broadcast_index(k,shape,inshape)));}
        else throw std::runtime_error("unknown frozen primitive");
        if(!s)throw std::runtime_error("malformed primitive input");
        std::cout<<id<<' '<<out.size();for(I v:out)std::cout<<' '<<v;std::cout<<'\n';
    }
    return 0;
}
int main(int argc,char**argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--primitives")return primitive_run(argv[2]);
        if(argc==3)return model_run(argv[1],argv[2]);
        throw std::runtime_error("usage: scalar_executor PACKAGE TRACE_DIR, or --primitives LUT");
    } catch(const std::exception& e) {std::cerr<<e.what()<<std::endl;return 2;}
}
