# Modified SQUAD for non-uniform timestamps

The SQUAD algorithm is a well-known algorithm for smooth interpolation of rotations.

The standard and simple algorithm for rotation is SLERP,
which ensures constant angular velocity over a given interpolation segment.
However, the flaw of SLERP for camera interpolation is that the angular velocity
is not continuous, and it will abruptly change on a new segment.
This problem is solved by SQUAD, but in its naive implementation, the length of
each segment must be uniform, otherwise the derivation fails.

I spent some time studying the underlying math and derived a formula that works for
non-uniform timestamps as well.

## The standard runtime algorithm

In SQUAD, each key-frame point is represented as
a quaternion q<sub>k</sub> at timestamp t<sub>k</sub>.
At each timestamp, we also pre-compute a
helper control point q<sup>c</sup><sub>k</sub>,
which derivation will be explored further below.

We are given the implementation:

squad<sub>k</sub>(t) = slerp(slerp(q<sub>k</sub>, q<sub>k+1</sub>, t),
    slerp(q<sup>c</sup><sub>k</sub>, q<sup>c</sup><sub>k+1</sub>, t),
    2t(1 - t))

t is given here in the range [0, 1), and is computed by:

t = (T - t<sub>k</sub>) / (t<sub>k+1</sub> - t<sub>k</sub>)

where T is the global time for which to evaluate.
An animation clip is stitched together by many such splines, one for each k.

### Analyze the expression

To perform further calculus on the squad(t) function, we can simplify to scalars.
If we assume for the purposes of analysis that all
the rotations have the same axis of rotation, we can
rewrite squad(t) to a linear interpolation of rotation angle &#952;, which
each key-frame now represents:

squad<sub>k</sub>(t) = lerp(lerp(&#952;<sub>k</sub>, &#952;<sub>k+1</sub>, t),
    lerp(&#952;<sup>c</sup><sub>k</sub>, &#952;<sup>c</sup><sub>k+1</sub>, t),
    2t(1 - t))

All these lerps are trivial expressions:

lerp(a, b, t) = (1 - t)a + tb

We can expand the expression and compute their first and second order derivatives.

v<sub>k</sub>(t) =
    (-3 + 8t - 6t<sup>2</sup>) &#952;<sub>k</sub> +
    (-1 - 4t + 6t<sup>2</sup>) &#952;<sub>k+1</sub> +
    (2 - 8t + 6t<sup>2</sup>) &#952;<sup>c</sup><sub>k</sub> +
    (4t - 6t<sup>2</sup>) &#952;<sup>c</sup><sub>k+1</sub>

a<sub>k</sub>(t) =
    (8 - 12t) &#952;<sub>k</sub> +
    (-4 + 12t) &#952;<sub>k+1</sub> +
    (-8 + 12t) &#952;<sup>c</sup><sub>k</sub> +
    (4 - 12t) &#952;<sup>c</sup><sub>k+1</sub>

It is important to note here that we derive with respect to the spline local parameter t.
To obtain the absolute angular velocity and acceleration at time t, we need to apply chain rules:

V<sub>k</sub>(t) = v<sub>k</sub>(t) (dt / dT) = v<sub>k</sub>(t) / d<sub>k</sub>

where d<sub>k</sub> = t<sub>k+1</sub> - t<sub>k</sub>,
and V<sub>k</sub>(t) is absolute angular velocity, d&#952; / dT.

Similarly, A<sub>k</sub>(t) = a<sub>k</sub>(t) / d<sub>k</sub><sup>2</sup> is
absolute angular acceleration, d<sup>2</sup>&#952; / (dT)<sup>2</sup>. When d<sub>k</sub> is constant,
all uses of d<sub>k</sub> cancel out,
and this is the assumption various algorithms online make.
To ensure first order continuity we must satisfy
V<sub>k</sub>(1) = V<sub>k+1</sub>(0), or alternatively if d<sub>k</sub> is constant,
v<sub>k</sub>(1) = v<sub>k+1</sub>(0). Similarly, if we want to ensure continuous second order derivative, we must
satisfy A<sub>k</sub>(1) = A<sub>k+1</sub>(0).

**v<sub>k+1</sub>(0)** =
    -3&#952;<sub>k+1</sub> +
    1&#952;<sub>k+2</sub> +
    2&#952;<sup>c</sup><sub>k+1</sub> +
    0&#952;<sup>c</sup><sub>k+2</sub> =\
    **(&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) -
    2(&#952;<sub>k+1</sub> - &#952;<sup>c</sup><sub>k+1</sub>)**

**v<sub>k</sub>(1)** =
    -1&#952;<sub>k</sub> +
    3&#952;<sub>k+1</sub> +
    0&#952;<sup>c</sup><sub>k</sub> -
    2&#952;<sup>c</sup><sub>k+1</sub> =\
    **(&#952;<sub>k+1</sub> - &#952;<sub>k</sub>) +
    2(&#952;<sub>k+1</sub> - &#952;<sup>c</sup><sub>k+1</sub>)**

**a<sub>k+1</sub>(0)** =
    8&#952;<sub>k+1</sub> -
    4&#952;<sub>k+2</sub> -
    8&#952;<sup>c</sup><sub>k+1</sub> +
    4&#952;<sup>c</sup><sub>k+2</sub> =\
    **8(&#952;<sub>k+1</sub> - &#952;<sup>c</sup><sub>k+1</sub>) -
    4(&#952;<sub>k+2</sub> - &#952;<sup>c</sup><sub>k+2</sub>)**

**a<sub>k</sub>(1)** =
    -4&#952;<sub>k</sub> +
    8&#952;<sub>k+1</sub> +
    4&#952;<sup>c</sup><sub>k</sub> -
    8&#952;<sup>c</sup><sub>k+1</sub> =\
    **8(&#952;<sub>k+1</sub> - &#952;<sup>c</sup><sub>k+1</sub>) -
    4(&#952;<sub>k</sub> - &#952;<sup>c</sup><sub>k</sub>)**

Based on these expressions, we can already intuit what the relationship
between the control points and the key-frame points are. The difference expresses
acceleration. With positive acceleration, the control points lags behind the key-frame, and vice versa.
Looking at the velocity expressions, with positive acceleration, we also get larger velocity at t = 1 compared to t = 0,
as expected.

To satisfy the velocity equations, we need to choose v<sub>k+1</sub>(0) = v<sub>k</sub>(1), so\
(&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) -
2(&#952;<sub>k+1</sub> - &#952;<sup>c</sup><sub>k+1</sub>) =
(&#952;<sub>k+1</sub> - &#952;<sub>k</sub>) +
2(&#952;<sub>k+1</sub> - &#952;<sup>c</sup><sub>k+1</sub>)\
(&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) -
(&#952;<sub>k+1</sub> - &#952;<sub>k</sub>) =
4(&#952;<sub>k+1</sub> - &#952;<sup>c</sup><sub>k+1</sub>)\
((&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) -
(&#952;<sub>k+1</sub> - &#952;<sub>k</sub>)) / 4 =
&#952;<sub>k+1</sub> - &#952;<sup>c</sup><sub>k+1</sub>\
&#952;<sub>k+1</sub> - ((&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) -
(&#952;<sub>k+1</sub> - &#952;<sub>k</sub>)) / 4 =
**&#952;<sup>c</sup><sub>k+1</sub>**

(&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) - (&#952;<sub>k+1</sub> - &#952;<sub>k</sub>)
is quite recognizable and intuitive.
This is the discrete measurement of acceleration at t<sub>k+1</sub>.

For simplicity of notation, we introduce the local delta,
&#916;<sub>k</sub> = &#952;<sub>k</sub> - &#952;<sup>c</sup><sub>k</sub>.
We can now rewrite the equations in a more digestable form:

v<sub>k+1</sub>(0) =
(&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) - 2&#916;<sub>k+1</sub>

v<sub>k</sub>(1) =
(&#952;<sub>k+1</sub> - &#952;<sub>k</sub>) + 2&#916;<sub>k+1</sub>

a<sub>k+1</sub>(0) =
8&#916;<sub>k+1</sub> - 4&#916;<sub>k+2</sub>

a<sub>k</sub>(1) =
8&#916;<sub>k+1</sub> - 4&#916;<sub>k</sub>

This equation will only yield a continuous acceleration if
&#916;<sub>k</sub> = &#916;<sub>k+2</sub>, which is not guaranteed.
However, we have the nice property that a constant acceleration will
yield a constant a<sub>k</sub>(t) for any k equal to 4&#916;<sub>k</sub>.
As we deduced earlier, &#916;<sub>k</sub> is 1/4th the measured discrete acceleration,
so everything checks out. Continuous acceleration is a nice property,
but not required for smooth camera motion.

## Going back to the quaternion domain

We have found expressions for the control points, but the derivation
has been happening in the angular domain, we need to work with quaternions.

Here, articles online will usually begin talking about logarithms and exponential functions of quaternions
which at first glance is pure non-sense, but it is actually fairly intuitive.
It took me a while to understand what the hell the article authors were smoking at first.

The insight is that **multiplying** two quaternions **adds** their rotational angles.
This is exactly the same as complex numbers, where multiplying two complex numbers
add their angles.
For logarithms of quaternions to work, we need to convert them to a number where
adding the results will function similarly to angular addition. Taking the exponent
should give us back the result.

q<sub>a</sub>q<sub>b</sub> = exp(ln(q<sub>a</sub>q<sub>b</sub>)) =
exp(ln(q<sub>a</sub>) + ln(q<sub>b</sub>))

As an aside, this extension also allows us to reason about powers of quaternions, since
ln(q<sub>a</sub><sup>c</sup>) = c&sdot;ln(q<sub>a</sub>).

The logarithm for a unit quaternion is computed as:

```
// vec3 quat_log(q)

if (abs(q.w) > 0.9999f)
    return vec3(0.0f);
else
    return normalize(q.as_vec4().xyz()) * acos(q.w);
```

The main confusion for me here is that quaternion multiplication does not commute,
but here the logarithm additions do. Subtraction might make more sense ...

```c++
// compute_inner_control_point_delta(q, delta)

quat inv_q1 = conjugate(q1);
quat delta_k = inv_q1 * q2; // q2 - q1
quat delta_k_minus1 = inv_q1 * q0; // q0 - q1 = -(q1 - q0)
vec3 delta_k_log = quat_log(delta_k);
vec3 delta_k_minus1_log = quat_log(delta_k_minus1);
vec3 delta = 0.25f * (delta_k_log + delta_k_minus1_log);
return delta;
```

The multiplication order seems somewhat arbitrary,
and I cannot prove exactly why we have to do it like this, but I cribbed
this part from the web. This document so far is trying to justify how it works.
At the very least, we can see similarities with the original derivation.
Here, `delta_k` and `delta_k_minus1` measure the velocities between key-frames.
Multiplying is "addition", but multiplying by conjugate is "subtraction".
By subtracting in the log-domain we can get an angular differential and measure acceleration.
As expected, we also take 1/4th since the delta is 1/4th measured acceleration.

This delta is later used to construct the control point q<sup>c</sup><sub>k</sub>.

```c++
// compute_inner_control_point(q, delta)

// Subtraction in angular domain.
return q * quat_exp(-delta);
```

which maps to the definition we made:

&#952;<sub>k</sub> - &#952;<sup>c</sup><sub>k</sub> =
&#916;<sub>k</sub> \
**&#952;<sup>c</sup><sub>k</sub> =
&#952;<sub>k</sub> - &#916;<sub>k</sub>**

To my great surprise, this is actually delightfully simple.
The logarithm is a vec3 where the direction is axis of rotation,
and length is the angle &#952;. This is what allows us to add/subtract the rotations
together. This style of expressing rotation is basically how we would
express torque in physics, nice!

The exponent just inverts what the log did.
We recover the angle by taking length of vector
and rebuilding the quaternion from that.

```
// quat quat_exp(q)

float l = dot(q, q);
if (l < 0.000001f)
{
    return quat(1.0f, 0.0f, 0.0f, 0.0f);
}
else
{
    float vlen = length(q);
    vec3 v = normalize(q) * sin(vlen);
    return quat(cos(vlen), v);
}
```

## Non-uniform time deltas d<sub>k</sub>

This is where it gets spicy and where I was initially stumped when
attempting to implement SQUAD. When constructing arbitrary camera paths,
it is helpful to be able to place key-frames at any timestamp.
What unfortunately happens now is that velocities are no longer
continuous over a spline boundary, because the splines are now swept at varying
rates. We will need to re-derive the control points,
based on V<sub>k</sub>(t), not v<sub>k</sub>(t), in angular domain.

V<sub>k+1</sub>(0) =
((&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) - 2&#916;<sub>k+1</sub>) / d<sub>k+1</sub>

V<sub>k</sub>(1) =
((&#952;<sub>k+1</sub> - &#952;<sub>k</sub>) + 2&#916;<sub>k+1</sub>) / d<sub>k</sub>

To be able to solve this, we need to consider that &#916;<sub>k+1</sub> need
not be a single value.
When evaluating spline k and k + 1, it can take different values as needed.
This gives rise to the "incoming" and "outgoing" control points.
The incoming control point delta is used when evaluating the previous spline.

V<sub>k+1</sub>(0) =
((&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) - 2&#916;<sup>o</sup><sub>k+1</sub>) / d<sub>k+1</sub>

V<sub>k</sub>(1) =
((&#952;<sub>k+1</sub> - &#952;<sub>k</sub>) + 2&#916;<sup>i</sup><sub>k+1</sub>) / d<sub>k</sub>

The superscript o and i denote outgoing and incoming respectively.
We can now solve this equation directly, similar to the derivation we did for constant d<sub>k</sub> earlier.

((&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) - 2&#916;<sup>o</sup><sub>k+1</sub>) / d<sub>k+1</sub> =
((&#952;<sub>k+1</sub> - &#952;<sub>k</sub>) + 2&#916;<sup>i</sup><sub>k+1</sub>) / d<sub>k</sub>

(&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) / d<sub>k+1</sub> -
2&#916;<sup>o</sup><sub>k+1</sub> / d<sub>k+1</sub> =
(&#952;<sub>k+1</sub> - &#952;<sub>k</sub>) / d<sub>k</sub> +
2&#916;<sup>i</sup><sub>k+1</sub> / d<sub>k</sub>

(&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>) / d<sub>k+1</sub> -
(&#952;<sub>k+1</sub> - &#952;<sub>k</sub>) / d<sub>k</sub> -
2&#916;<sup>o</sup><sub>k+1</sub> / d<sub>k+1</sub> =
2&#916;<sup>i</sup><sub>k+1</sub> / d<sub>k</sub>

If we let the ratio r be d<sub>k</sub> / d<sub>k+1</sub>, we get

&#916;<sup>i</sup><sub>k+1</sub> =
    ((&#952;<sub>k+2</sub> - &#952;<sub>k+1</sub>)(d<sub>k</sub> / d<sub>k+1</sub>) -
    (&#952;<sub>k+1</sub> - &#952;<sub>k</sub>)) / 2 -
    &#916;<sup>o</sup><sub>k+1</sub>(d<sub>k</sub> / d<sub>k+1</sub>)

Which shows that we can actually select the outgoing control point rather freely,
and we can then use this formula to compensate the difference in
d<sub>k</sub> in the incoming control point.

For the outgoing control point, we should modify the acceleration
computation to be aware of different step rates. Basically,
we normalize the discrete velocities in terms of global time T.
There might be better ways of computing this, but, meh.
From empiric testing, the result is pretty accurate.

```
quat inv_q1 = conjugate(q1);
quat delta_k = inv_q1 * q2; // q2 - q1
quat delta_k_minus1 = inv_q1 * q0; // q0 - q1 = -(q1 - q0)
vec3 delta_k_log = quat_log(delta_k);
vec3 delta_k_minus1_log = quat_log(delta_k_minus1);

// We sample velocity at the center of the segment when taking the difference.
// Future sample is at t = +1/2 dt
// Past sample is at t = -1/2 dt
float segment_time = 0.5f * (dt0 + dt1);
vec3 absolute_accel = (delta_k_log / dt1 + delta_k_minus1_log / dt0) / segment_time;
vec3 delta = (0.25f * dt1 * dt1) * absolute_accel;
```

```
// Computed from snippet above
vec3 outgoing = tmp_spline_deltas[i];

float dt0 = new_linear_timestamps[i] - new_linear_timestamps[i - 1];
float dt1 = i + 1 < n ? (new_linear_timestamps[i + 1] - new_linear_timestamps[i]) : dt0;
float t_ratio = dt0 / dt1;

const quat &q0 = new_linear_values[i - 1];
const quat &q1 = new_linear_values[i];
const quat &q2 = i + 1 < n ? new_linear_values[i + 1] : q1;

quat q12 = conjugate(q1) * q2;
quat q10 = conjugate(q1) * q0; // This is implicitly negated.
vec3 delta_q12 = quat_log(q12);
vec3 delta_q10 = quat_log(q10);

vec3 incoming = 0.5f * (t_ratio * delta_q12 + delta_q10) - t_ratio * outgoing;

spline_data[3 * spline + 0] = q1 * quat_exp(-incoming);
spline_data[3 * spline + 1] = q1;
spline_data[3 * spline + 2] = q1 * quat_exp(-outgoing);
```

Each key-frame gets 3 values. This is very similar to the
CUBICSPLINE formulation used in glTF.
When evalulating the spline in runtime we look at indices
3 * k + {1, 2, 3, 4}.

### Modified SQUAD function

squad<sub>k</sub>(t) = slerp(slerp(q<sub>k</sub>, q<sub>k+1</sub>, t),
slerp(q<sub>k</sub>&#183;quatExp(-&#916;<sup>o</sup><sub>k</sub>),
    q<sub>k+1</sub>&#183;quatExp(-&#916;<sup>i</sup><sub>k+1</sub>), t),
    2t(1 - t))

&#916; is in the log domain as we computed above with outgoing and incoming deltas.

## Verification

While doing this work, I also made a test bench of sorts to evaluate the results.
I tested 4 different scenarios with scalars.

- Interpolate quadratic function with even timestamps. Should be 100% exact.
- Interpolate quadratic function with uneven timestamps. Will have some error.
- Interpolate cubic function with even timestamps. Expect some errors due to non-constant acceleration.
- Interpolate cubic function with uneven timestamps. Expect some errors due to non-constant acceleration.

We want to validate:
- Average error of reference function f(t) and interpolated result.
- Continuity of measured first derivative (and second derivative).

### Quadratic

f(t) = 0.5t - 0.25t<sup>2</sup>

#### Even timestamps

Key frames placed at t = {0, 0.5, 1.0, 2.0, 2.5, 3.0}.\
Perfect result. As expected.

#### Uneven timestamps
Key frames placed at t = {0, 1.0, 1.8, 2.1, 2.9, 3.0, 4.2, 4.3, 5.0, 6.0}.\
Average error: 0.008141\
Continuous first derivative, discontinuous second derivative.

### Cubic

f(t) = 0.5t - 0.25t<sup>2</sup> + 0.25t<sup>3</sup>

#### Even timestamps
Key frames placed at t = {0, 0.5, 1.0, 2.0, 2.5, 3.0}.\
Average error: 0.00195\
Continuous first derivative, discontinuous second derivative.

#### Uneven timestamps
Key frames placed at t = {0, 0.5, 0.9, 1.1, 1.4, 1.5, 2.1, 2.2, 2.5, 3.0}.\
Average error: 0.008285\
Continuous first derivative, discontinuous second derivative.

### Summary

The more even timestamps we have, the more accurate the spline becomes.
The error is also quite acceptable, and we see continuous first derivative,
which is the critical part to get right.