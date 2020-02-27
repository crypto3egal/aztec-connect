#include "./field.hpp"

#include "../../../assert.hpp"
#include "../../../curves/bn254/fr.hpp"

#include "../../composer/mimc_composer.hpp"
#include "../../composer/standard_composer.hpp"
#include "../../composer/turbo_composer.hpp"

#include "../bool/bool.hpp"

namespace plonk {
namespace stdlib {

template <typename ComposerContext>
field_t<ComposerContext>::field_t(ComposerContext* parent_context)
    : context(parent_context)
    , additive_constant(barretenberg::fr::field_t::zero)
    , multiplicative_constant(barretenberg::fr::field_t::one)
    , witness_index(static_cast<uint32_t>(-1))
{}

template <typename ComposerContext>
field_t<ComposerContext>::field_t(const witness_t<ComposerContext>& value)
    : context(value.context)
{
    additive_constant = barretenberg::fr::field_t::zero;
    multiplicative_constant = barretenberg::fr::field_t::one;
    witness_index = value.witness_index;
}

template <typename ComposerContext>
field_t<ComposerContext>::field_t(ComposerContext* parent_context, const barretenberg::fr::field_t& value)
    : context(parent_context)
{
    barretenberg::fr::field_t::__copy(value, additive_constant);
    multiplicative_constant = barretenberg::fr::field_t::zero;
    witness_index = static_cast<uint32_t>(-1);
}

template <typename ComposerContext>
field_t<ComposerContext>::field_t(const uint64_t value)
    : context(nullptr)
{
    additive_constant = barretenberg::fr::field_t{ value, 0UL, 0UL, 0UL }.to_montgomery_form();
    multiplicative_constant = barretenberg::fr::field_t::zero;
    witness_index = static_cast<uint32_t>(-1);
}

template <typename ComposerContext>
field_t<ComposerContext>::field_t(const field_t& other)
    : context(other.context)
{
    barretenberg::fr::field_t::__copy(other.additive_constant, additive_constant);
    barretenberg::fr::field_t::__copy(other.multiplicative_constant, multiplicative_constant);
    witness_index = other.witness_index;
}

template <typename ComposerContext>
field_t<ComposerContext>::field_t(field_t&& other)
    : context(other.context)
{
    barretenberg::fr::field_t::__copy(other.additive_constant, additive_constant);
    barretenberg::fr::field_t::__copy(other.multiplicative_constant, multiplicative_constant);
    witness_index = other.witness_index;
}

template <typename ComposerContext> field_t<ComposerContext>::field_t(const bool_t<ComposerContext>& other)
{
    context = (other.context == nullptr) ? nullptr : other.context;
    if (other.witness_index == static_cast<uint32_t>(-1)) {
        additive_constant = (other.witness_bool ^ other.witness_inverted) ? barretenberg::fr::field_t::one
                                                                          : barretenberg::fr::field_t::zero;
        multiplicative_constant = barretenberg::fr::field_t::one;
        witness_index = static_cast<uint32_t>(-1);
    } else {
        witness_index = other.witness_index;
        additive_constant = other.witness_inverted ? barretenberg::fr::field_t::one : barretenberg::fr::field_t::zero;
        multiplicative_constant =
            other.witness_inverted ? barretenberg::fr::field_t::neg_one : barretenberg::fr::field_t::one;
    }
}

template <typename ComposerContext>
field_t<ComposerContext>::field_t(byte_array<ComposerContext> const& other)
    : context(other.get_context())
    , additive_constant(barretenberg::fr::field_t::zero)
    , multiplicative_constant(barretenberg::fr::field_t::one)
    , witness_index(static_cast<uint32_t>(-1))
{
    auto bits = other.bits();

    barretenberg::fr::field_t two = barretenberg::fr::field_t{ 2, 0, 0, 0 }.to_montgomery_form();

    for (size_t i = 0; i < bits.size(); ++i) {
        field_t<ComposerContext> temp(bits[i].context);
        if (bits[i].is_constant()) {
            temp.additive_constant =
                bits[i].get_value() ? barretenberg::fr::field_t::one : barretenberg::fr::field_t::zero;
        } else {
            temp.witness_index = bits[i].witness_index;
        }
        barretenberg::fr::field_t scaling_factor_value = two.pow(static_cast<uint64_t>(255 - i));
        field_t<ComposerContext> scaling_factor(bits[i].context, scaling_factor_value);
        *this = *this + (scaling_factor * temp);
    }
}

template <typename ComposerContext> field_t<ComposerContext>::operator bool_t<ComposerContext>()
{
    if (witness_index == static_cast<uint32_t>(-1)) {
        bool_t<ComposerContext> result(context);
        result.witness_bool = (additive_constant == barretenberg::fr::field_t::one);
        result.witness_inverted = false;
        result.witness_index = static_cast<uint32_t>(-1);
        return result;
    }
    bool add_constant_check = (additive_constant == barretenberg::fr::field_t::zero);
    bool mul_constant_check = (multiplicative_constant == barretenberg::fr::field_t::one);
    bool inverted_check = (additive_constant == barretenberg::fr::field_t::one) &&
                          (multiplicative_constant == barretenberg::fr::field_t::neg_one);
    if ((!add_constant_check || !mul_constant_check) && !inverted_check) {
        normalize();
    }

    barretenberg::fr::field_t witness = context->get_variable(witness_index);
    ASSERT((witness == barretenberg::fr::field_t::zero) || (witness == barretenberg::fr::field_t::one));
    bool_t<ComposerContext> result(context);
    result.witness_bool = (witness == barretenberg::fr::field_t::one);
    result.witness_inverted = inverted_check;
    result.witness_index = witness_index;
    context->create_bool_gate(witness_index);
    return result;
}

template <typename ComposerContext> field_t<ComposerContext>::operator byte_array<ComposerContext>() const
{
    barretenberg::fr::field_t value = get_value().from_montgomery_form();
    typename byte_array<ComposerContext>::bits_t bits(256, bool_t(context));

    if (is_constant()) {
        for (size_t i = 0; i < 256; ++i) {
            bits[i] = value.get_bit(255 - i);
        }
    } else {
        barretenberg::fr::field_t two = barretenberg::fr::field_t{ 2, 0, 0, 0 }.to_montgomery_form();
        field_t<ComposerContext> validator(context, barretenberg::fr::field_t::zero);

        for (size_t i = 0; i < 256; ++i) {
            bool_t bit = witness_t(context, value.get_bit(255 - i));
            bits[i] = bit;
            barretenberg::fr::field_t scaling_factor_value = two.pow(static_cast<uint64_t>(255 - i));
            field_t<ComposerContext> scaling_factor(context, scaling_factor_value);
            validator = validator + (scaling_factor * bit);
        }

        context->assert_equal(validator.witness_index, witness_index);
    }

    return byte_array<ComposerContext>(context, bits);
}

template <typename ComposerContext> field_t<ComposerContext>& field_t<ComposerContext>::operator=(const field_t& other)
{
    barretenberg::fr::field_t::__copy(other.additive_constant, additive_constant);
    barretenberg::fr::field_t::__copy(other.multiplicative_constant, multiplicative_constant);
    witness_index = other.witness_index;
    context = (other.context == nullptr ? nullptr : other.context);
    return *this;
}

template <typename ComposerContext> field_t<ComposerContext>& field_t<ComposerContext>::operator=(field_t&& other)
{
    barretenberg::fr::field_t::__copy(other.additive_constant, additive_constant);
    barretenberg::fr::field_t::__copy(other.multiplicative_constant, multiplicative_constant);
    witness_index = other.witness_index;
    context = (other.context == nullptr ? nullptr : other.context);
    return *this;
}

template <typename ComposerContext>
field_t<ComposerContext> field_t<ComposerContext>::operator+(const field_t& other) const
{
    ComposerContext* ctx = (context == nullptr) ? other.context : context;
    field_t<ComposerContext> result(ctx);
    ASSERT(ctx || (witness_index == static_cast<uint32_t>(-1) && other.witness_index == static_cast<uint32_t>(-1)));

    if (witness_index == other.witness_index) {
        result.additive_constant = additive_constant + other.additive_constant;
        result.multiplicative_constant = multiplicative_constant + other.multiplicative_constant;
        result.witness_index = witness_index;
    } else if (witness_index == static_cast<uint32_t>(-1) && other.witness_index == static_cast<uint32_t>(-1)) {
        // both inputs are constant - don't add a gate
        result.additive_constant = additive_constant + other.additive_constant;
    } else if (witness_index != static_cast<uint32_t>(-1) && other.witness_index == static_cast<uint32_t>(-1)) {
        // one input is constant - don't add a gate, but update scaling factors
        result.additive_constant = additive_constant + other.additive_constant;
        barretenberg::fr::field_t::__copy(multiplicative_constant, result.multiplicative_constant);
        result.witness_index = witness_index;
    } else if (witness_index == static_cast<uint32_t>(-1) && other.witness_index != static_cast<uint32_t>(-1)) {
        result.additive_constant = additive_constant + other.additive_constant;
        barretenberg::fr::field_t::__copy(other.multiplicative_constant, result.multiplicative_constant);
        result.witness_index = other.witness_index;
    } else {
        barretenberg::fr::field_t T0;
        barretenberg::fr::field_t left = context->get_variable(witness_index);
        barretenberg::fr::field_t right = context->get_variable(other.witness_index);
        barretenberg::fr::field_t out;
        out = left * multiplicative_constant;
        T0 = right * other.multiplicative_constant;
        out += T0;
        out += additive_constant;
        out += other.additive_constant;
        result.witness_index = ctx->add_variable(out);

        const waffle::add_triple gate_coefficients{ witness_index,
                                                    other.witness_index,
                                                    result.witness_index,
                                                    multiplicative_constant,
                                                    other.multiplicative_constant,
                                                    barretenberg::fr::field_t::neg_one,
                                                    (additive_constant + other.additive_constant) };
        ctx->create_add_gate(gate_coefficients);
    }
    return result;
}

template <typename ComposerContext>
field_t<ComposerContext> field_t<ComposerContext>::operator-(const field_t& other) const
{
    field_t<ComposerContext> rhs(other);
    rhs.additive_constant.self_neg();
    rhs.multiplicative_constant.self_neg();
    return operator+(rhs);
}

template <typename ComposerContext>
field_t<ComposerContext> field_t<ComposerContext>::operator*(const field_t& other) const
{
    ComposerContext* ctx = (context == nullptr) ? other.context : context;
    field_t<ComposerContext> result(ctx);
    ASSERT(ctx || (witness_index == static_cast<uint32_t>(-1) && other.witness_index == static_cast<uint32_t>(-1)));

    if (witness_index == static_cast<uint32_t>(-1) && other.witness_index == static_cast<uint32_t>(-1)) {
        // both inputs are constant - don't add a gate
        result.additive_constant = additive_constant * other.additive_constant;
    } else if (witness_index != static_cast<uint32_t>(-1) && other.witness_index == static_cast<uint32_t>(-1)) {
        // one input is constant - don't add a gate, but update scaling factors
        result.additive_constant = additive_constant * other.additive_constant;
        result.multiplicative_constant = multiplicative_constant * other.additive_constant;
        result.witness_index = witness_index;
    } else if (witness_index == static_cast<uint32_t>(-1) && other.witness_index != static_cast<uint32_t>(-1)) {
        result.additive_constant = additive_constant * other.additive_constant;
        result.multiplicative_constant = other.multiplicative_constant * additive_constant;
        result.witness_index = other.witness_index;
    } else {
        // both inputs map to circuit varaibles - create a * constraint
        barretenberg::fr::field_t T0;
        barretenberg::fr::field_t q_m;
        barretenberg::fr::field_t q_l;
        barretenberg::fr::field_t q_r;
        barretenberg::fr::field_t q_c;

        q_c = additive_constant * other.additive_constant;
        q_r = additive_constant * other.multiplicative_constant;
        q_l = multiplicative_constant * other.additive_constant;
        q_m = multiplicative_constant * other.multiplicative_constant;

        barretenberg::fr::field_t left = context->get_variable(witness_index);
        barretenberg::fr::field_t right = context->get_variable(other.witness_index);
        barretenberg::fr::field_t out;

        out = left * right;
        out *= q_m;
        T0 = left * q_l;
        out += T0;
        T0 = right * q_r;
        out += T0;
        out += q_c;
        result.witness_index = ctx->add_variable(out);
        const waffle::poly_triple gate_coefficients{ witness_index,
                                                     other.witness_index,
                                                     result.witness_index,
                                                     q_m,
                                                     q_l,
                                                     q_r,
                                                     barretenberg::fr::field_t::neg_one,
                                                     q_c };
        ctx->create_poly_gate(gate_coefficients);
    }
    return result;
}

template <typename ComposerContext>
field_t<ComposerContext> field_t<ComposerContext>::operator/(const field_t& other) const
{
    ComposerContext* ctx = (context == nullptr) ? other.context : context;
    field_t<ComposerContext> result(ctx);
    ASSERT(ctx || (witness_index == static_cast<uint32_t>(-1) && other.witness_index == static_cast<uint32_t>(-1)));

    barretenberg::fr::field_t additive_multiplier = barretenberg::fr::field_t::one;

    if (witness_index == static_cast<uint32_t>(-1) && other.witness_index == static_cast<uint32_t>(-1)) {
        // both inputs are constant - don't add a gate
        if (!(other.additive_constant == barretenberg::fr::field_t::zero)) {
            additive_multiplier = other.additive_constant.invert();
        }
        result.additive_constant = additive_constant * additive_multiplier;
    } else if (witness_index != static_cast<uint32_t>(-1) && other.witness_index == static_cast<uint32_t>(-1)) {
        // one input is constant - don't add a gate, but update scaling factors
        if (!(other.additive_constant == barretenberg::fr::field_t::zero)) {
            additive_multiplier = other.additive_constant.invert();
        }
        result.additive_constant = additive_constant * additive_multiplier;
        result.multiplicative_constant = multiplicative_constant * additive_multiplier;
        result.witness_index = witness_index;
    } else if (witness_index == static_cast<uint32_t>(-1) && other.witness_index != static_cast<uint32_t>(-1)) {
        if (!(other.additive_constant == barretenberg::fr::field_t::zero)) {
            additive_multiplier = other.additive_constant.invert();
        }
        result.additive_constant = additive_constant * other.additive_constant;
        result.multiplicative_constant = other.multiplicative_constant * additive_constant;
        result.witness_index = other.witness_index;
    } else {
        barretenberg::fr::field_t left = context->get_variable(witness_index);
        barretenberg::fr::field_t right = context->get_variable(other.witness_index);
        barretenberg::fr::field_t out;

        // even if LHS is constant, if divisor is not constant we need a gate to compute the inverse
        // barretenberg::fr::field_t witness_multiplier = other.witness.invert();
        // m1.x1 + a1 / (m2.x2 + a2) = x3
        barretenberg::fr::field_t T0;
        T0 = multiplicative_constant * left;
        T0 += additive_constant;
        barretenberg::fr::field_t T1;
        T1 = other.multiplicative_constant * right;
        T1 += other.additive_constant;

        out = T0 * T1.invert();
        result.witness_index = ctx->add_variable(out);

        // m2.x2.x3 + a2.x3 = m1.x1 + a1
        // m2.x2.x3 + a2.x3 - m1.x1 - a1 = 0
        // left = x3
        // right = x2
        // out = x1
        // qm = m2
        // ql = a2
        // qr = 0
        // qo = -m1
        // qc = -a1
        barretenberg::fr::field_t q_m = other.multiplicative_constant;
        barretenberg::fr::field_t q_l = other.additive_constant;
        barretenberg::fr::field_t q_r = barretenberg::fr::field_t::zero;
        barretenberg::fr::field_t q_o = -multiplicative_constant;
        barretenberg::fr::field_t q_c = -additive_constant;

        const waffle::poly_triple gate_coefficients{
            result.witness_index, other.witness_index, witness_index, q_m, q_l, q_r, q_o, q_c
        };
        ctx->create_poly_gate(gate_coefficients);
    }
    return result;
}

template <typename ComposerContext> field_t<ComposerContext> field_t<ComposerContext>::normalize() const
{
    if (witness_index == static_cast<uint32_t>(-1) || ((multiplicative_constant == barretenberg::fr::field_t::one) &&
                                                       (additive_constant == barretenberg::fr::field_t::zero))) {
        return *this;
    }

    field_t<ComposerContext> result(context);
    barretenberg::fr::field_t value = context->get_variable(witness_index);
    barretenberg::fr::field_t out;
    out = value * multiplicative_constant;
    out += additive_constant;

    result.witness_index = context->add_variable(out);
    result.additive_constant = barretenberg::fr::field_t::zero;
    result.multiplicative_constant = barretenberg::fr::field_t::one;
    const waffle::add_triple gate_coefficients{ witness_index,
                                                witness_index,
                                                result.witness_index,
                                                multiplicative_constant,
                                                0,
                                                barretenberg::fr::field_t::neg_one,
                                                additive_constant };

    context->create_add_gate(gate_coefficients);
    return result;
}

template <typename ComposerContext> bool_t<ComposerContext> field_t<ComposerContext>::is_zero()
{
    if (witness_index == static_cast<uint32_t>(-1)) {
        return bool_t(context, (get_value() == barretenberg::fr::field_t::zero));
    }

    // To check whether a field element, k, is zero, we use the fact that, if k > 0,
    // there exists a modular inverse k', such that k * k' = 1

    // To verify whether k = 0, we must do 2 checks
    // First is that (k * k') - 1 + is_zero = 0

    // If is_zero = false, then k' must be the modular inverse of k, therefore k is not 0

    // If is_zero = true, then either k or k' is zero (or both)
    // To ensure that it is k that is zero, and not k', we must apply
    // an additional check: that if is_zero = true, k' = 1
    // This way, if (k * k') = 0, we know that k = 0.
    // The second check is: (is_zero * k') - is_zero = 0
    field_t k = normalize();
    bool_t is_zero = witness_t(context, (k.get_value() == barretenberg::fr::field_t::zero));
    field_t k_inverse;
    if (is_zero.get_value()) {
        k_inverse = witness_t(context, barretenberg::fr::field_t::one);
    } else {
        barretenberg::fr::field_t k_inverse_value = k.get_value().invert();
        k_inverse = witness_t(context, k_inverse_value);
    }

    // k * k_inverse + is_zero - 1 = 0
    barretenberg::fr::field_t q_m = barretenberg::fr::field_t::one;
    barretenberg::fr::field_t q_l = barretenberg::fr::field_t::zero;
    barretenberg::fr::field_t q_r = barretenberg::fr::field_t::zero;
    barretenberg::fr::field_t q_o = barretenberg::fr::field_t::one;
    barretenberg::fr::field_t q_c = barretenberg::fr::field_t::neg_one;
    const waffle::poly_triple gate_coefficients_a{
        k.witness_index, k_inverse.witness_index, is_zero.witness_index, q_m, q_l, q_r, q_o, q_c
    };
    context->create_poly_gate(gate_coefficients_a);

    // is_zero * k_inverse - is_zero = 0
    q_o = barretenberg::fr::field_t::neg_one;
    q_c = barretenberg::fr::field_t::zero;
    const waffle::poly_triple gate_coefficients_b{
        is_zero.witness_index, k_inverse.witness_index, is_zero.witness_index, q_m, q_l, q_r, q_o, q_c
    };
    context->create_poly_gate(gate_coefficients_b);
    return is_zero;
}

template <typename ComposerContext> barretenberg::fr::field_t field_t<ComposerContext>::get_value() const
{
    if (witness_index != static_cast<uint32_t>(-1)) {
        ASSERT(context != nullptr);
        return (multiplicative_constant * context->get_variable(witness_index)) + additive_constant;
    } else {
        return additive_constant;
    }
}

template <typename ComposerContext>
bool_t<ComposerContext> field_t<ComposerContext>::operator==(const field_t& other) const
{
    ComposerContext* ctx = (context == nullptr) ? other.context : context;

    if (is_constant() && other.is_constant()) {
        return (get_value() == other.get_value());
    }

    barretenberg::fr::field_t fa = get_value();
    barretenberg::fr::field_t fb = other.get_value();
    barretenberg::fr::field_t fd = fa - fb;
    bool is_equal = (fa == fb);
    barretenberg::fr::field_t fc = is_equal ? barretenberg::fr::field_t::one : fd.invert();

    bool_t result(witness_t(ctx, is_equal));
    field_t c(witness_t(ctx, fc));
    field_t d = *this - other;
    field_t test_lhs = d * c;
    field_t test_rhs = (field_t(ctx, barretenberg::fr::field_t::one) - result);
    test_rhs = test_rhs.normalize();
    ctx->assert_equal(test_lhs.witness_index, test_rhs.witness_index);

    barretenberg::fr::field_t fe = is_equal ? barretenberg::fr::field_t::one : fd;
    field_t e(witness_t(ctx, fe));

    // Ensures c is never 0.
    barretenberg::fr::field_t q_m = barretenberg::fr::field_t::one;
    barretenberg::fr::field_t q_l = barretenberg::fr::field_t::zero;
    barretenberg::fr::field_t q_r = barretenberg::fr::field_t::zero;
    barretenberg::fr::field_t q_c = barretenberg::fr::field_t::neg_one;
    barretenberg::fr::field_t q_o = barretenberg::fr::field_t::zero;
    const waffle::poly_triple gate_coefficients{
        c.witness_index, e.witness_index, c.witness_index, q_m, q_l, q_r, q_o, q_c
    };
    ctx->create_poly_gate(gate_coefficients);

    return result;
}

template class field_t<waffle::StandardComposer>;
template class field_t<waffle::MiMCComposer>;
template class field_t<waffle::TurboComposer>;

} // namespace stdlib
} // namespace plonk
