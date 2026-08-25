#include "ubcm/addressing.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ubcm {
namespace {
BitVector bits(const char* s) { return *BitVector::from_bit_string(s); }
CodecError cerr(CodecErrorCode c, std::uint64_t p, const char* m) { return {c, p, m}; }
template<class T> CodecResult<T> fail(BitCursor& cur, std::uint64_t p, CodecErrorCode c, const char* m) {
    (void)cur.seek(p); return std::unexpected(cerr(c, p, m));
}
CodecResult<RegisterClass> read_class(BitCursor& c) {
    auto v=c.read_uint(2); if(!v) return std::unexpected(v.error());
    if(*v>3) return std::unexpected(cerr(CodecErrorCode::invalid_data,c.position()-2,"invalid register class"));
    return static_cast<RegisterClass>(*v);
}
}

BitVector encode_register_selector(const RegisterSelector& s) {
    BitVector out; out.append(bits(s.class_id==RegisterClass::procedure ? "00" : s.class_id==RegisterClass::superlocal ? "01" : s.class_id==RegisterClass::local ? "10" : "11"));
    if(s.class_id!=RegisterClass::procedure) out.append(encode_bit_string(s.name));
    return out;
}

CodecResult<RegisterSelector> decode_register_selector(BitCursor& c) {
    const auto start=c.position(); auto cl=read_class(c); if(!cl) return fail<RegisterSelector>(c,start,cl.error().code,cl.error().message.c_str());
    if(*cl==RegisterClass::procedure) return RegisterSelector{*cl,{}};
    auto n=decode_bit_string(c); if(!n) return std::unexpected(n.error()); return RegisterSelector{*cl,std::move(*n)};
}

BitVector encode_register_address(const EncodedRegisterAddress& a) { auto out=encode_register_selector(a.selector); out.append(encode_size(a.bit_offset)); return out; }
CodecResult<EncodedRegisterAddress> decode_register_address(BitCursor& c) {
    auto s=decode_register_selector(c); if(!s) return std::unexpected(s.error()); auto o=decode_size(c); if(!o) return std::unexpected(o.error()); return EncodedRegisterAddress{std::move(*s),*o};
}

BitVector encode_reference(const AddressReference& r) {
    BitVector out;
    std::visit([&](const auto& v){ using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,std::uint64_t>) { out.append(bits("00")); out.append(encode_var_uint(v)); }
        else if constexpr(std::is_same_v<T,IndirectReference>) { out.append(bits("01")); out.append(encode_register_address(v.pointer)); }
        else if constexpr(std::is_same_v<T,DirectReference>) { out.append(bits("10")); out.append(encode_register_address(v.address)); }
        else { out.append(bits("11")); out.append(encode_size(v.depth)); out.push_back(v.indirect); out.append(encode_bit_string(v.local_name)); out.append(encode_size(v.bit_offset)); }
    },r); return out;
}
CodecResult<AddressReference> decode_reference(BitCursor& c) {
    auto m=c.read_uint(2); if(!m) return std::unexpected(m.error());
    if(*m==0){auto v=decode_var_uint(c);if(!v)return std::unexpected(v.error());return AddressReference{*v};}
    if(*m==1||*m==2){auto a=decode_register_address(c);if(!a)return std::unexpected(a.error()); if(*m==1)return AddressReference{IndirectReference{*a}}; return AddressReference{DirectReference{*a}};}
    auto d=decode_size(c);if(!d)return std::unexpected(d.error());auto b=c.read_bit();if(!b)return std::unexpected(b.error());auto n=decode_bit_string(c);if(!n)return std::unexpected(n.error());auto o=decode_size(c);if(!o)return std::unexpected(o.error());return AddressReference{ForeignReference{*d,*b,std::move(*n),*o}};
}

BitVector encode_source(const SourceOperand& s){ if(s.immediate){if(!std::holds_alternative<std::uint64_t>(s.reference))throw std::invalid_argument("immediate source must be integer");return encode_reference(s.reference);} auto out=encode_reference(s.reference);out.append(encode_size(s.bit_count));return out; }
CodecResult<SourceOperand> decode_source(BitCursor& c){auto p=c.position();auto m=c.read_uint(2);if(!m)return std::unexpected(m.error());if(auto reset=c.seek(p);!reset)return std::unexpected(reset.error());if(*m==0){auto r=decode_reference(c);if(!r)return std::unexpected(r.error());if(!std::holds_alternative<std::uint64_t>(*r))return std::unexpected(cerr(CodecErrorCode::invalid_data,p,"invalid immediate"));return SourceOperand{*r,0,true};}auto r=decode_reference(c);if(!r)return std::unexpected(r.error());auto n=decode_size(c);if(!n)return std::unexpected(n.error());return SourceOperand{*r,*n,false};}
BitVector encode_destination(const DestinationOperand& d){if(std::holds_alternative<std::uint64_t>(d.reference))throw std::invalid_argument("destination cannot be immediate");return encode_reference(d.reference);}
CodecResult<DestinationOperand> decode_destination(BitCursor& c){auto r=decode_reference(c);if(!r)return std::unexpected(r.error());if(std::holds_alternative<std::uint64_t>(*r))return std::unexpected(cerr(CodecErrorCode::invalid_data,c.position(),"destination cannot be immediate"));return DestinationOperand{*r};}

RegisterResult<RegisterHandle> resolve_selector(const RegisterSelector& s,const ResolutionContext& x){const NameResolver* r=nullptr;switch(s.class_id){case RegisterClass::procedure:r=x.procedure;break;case RegisterClass::superlocal:r=x.superlocal;break;case RegisterClass::local:r=x.local;break;case RegisterClass::global:r=x.global;break;}if(!r)return std::unexpected(RegisterError{RegisterError::Code::not_found,"resolver is unavailable"});return r->resolve(s.name);}
}
