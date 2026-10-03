# Statistical Methodology

## Scope

This document specifies the statistical treatment used by the Evaluation Lab. The lab is a local, synthetic validation harness; its timing observations are used to study measurement stability and data-dependent differences, not to establish a universal hardware latency constant or a cryptographic security claim.

The implementation uses C++17 and deterministic pseudo-random seeds for bootstrap resampling so that the reported confidence intervals are reproducible for a fixed input sample set.

## 1. Sample alignment and block averaging

For paired traces with sample sequences A=(a_1,...,a_{n_A}) and B=(b_1,...,b_{n_B}), the paired comparison uses

n = min(n_A, n_B).

Only the first n observations from each class are retained for the pairwise comparison. This prevents unequal trace counts from silently changing the denominator of later estimators.

When an averaging window of width w is requested, adjacent observations are reduced to non-overlapping block means:

x_bar_{j,w} = (1/w) sum_{r=0}^{w-1} x_{jw+r+1},
j=0,...,floor(N/w)-1.

A partial trailing block is discarded.

## 2. Robust outlier rejection

The primary method is configurable between median absolute deviation (MAD) and Tukey IQR fences. Both methods are evaluated so that the choice itself is observable in the report.

### MAD

Let m=median(x_i). The median absolute deviation is

MAD = median(|x_i - m|).

The implementation uses the normal-consistency scaling factor 1.4826:

s_MAD = 1.4826 MAD.

An observation is retained when

|x_i - m| <= k s_MAD,

with k=3.5 by default.

The report records input count, retained count, rejected count, rejected percentage, mean, median, and sample standard deviation after filtering.

### IQR

The first and third quartiles are Q_1 and Q_3, with

IQR = Q_3 - Q_1.

For the default Tukey multiplier c=1.5, an observation is retained when

Q_1 - c IQR <= x_i <= Q_3 + c IQR.

The report gives MAD and IQR rejection counts and percentages side by side for both fixed and random classes. This makes the sensitivity of the result to the outlier rule auditable rather than hidden.

The robustness motivation follows the classical robust-statistics literature, where median-based estimators have bounded influence compared with mean/standard-deviation screening (Hampel, 1974).

## 3. Mean, median, and bootstrap confidence intervals

For each retained metric sample, the reported point estimates are

x_bar = (1/n) sum_i x_i

and

x_tilde = median(x_1,...,x_n).

Sampling uncertainty is estimated with the nonparametric bootstrap (Efron, 1979). Given B=10,000 resamples by default, each bootstrap replicate b is drawn with replacement from the retained empirical sample:

x_1^(b),...,x_n^(b).

For each replicate, both the mean and median are recomputed. The implementation reports the percentile 95% interval:

CI_95% = [Q_0.025(theta_hat^*), Q_0.975(theta_hat^*)],

where theta_hat is either the mean or median and Q_p is the empirical bootstrap quantile.

Bootstrap confidence intervals quantify estimator uncertainty; they do not correct for bias caused by a flawed measurement design, temporal dependence, or an unrepresentative sampling frame. For the same reason, a CPU benchmark CI should not be interpreted as a universal hardware specification.

The evaluation JSON reports the CI adjacent to the fixed/random latency mean and median. The generic bootstrap_ci API can also be applied to an independent vector of bandwidth observations at the component that actually collects those observations. The present Evaluation Lab does not receive bandwidth samples, so it does not fabricate a bandwidth CI from latency data.

## 4. Signal-to-noise ratio

The implementation defines a two-class amplitude SNR as

SNR = |x_bar_fixed - x_bar_random| /
      sqrt((s_fixed^2 + s_random^2)/2),

where s^2 denotes the unbiased sample variance after the configured robust filtering.

The decibel representation is

SNR_dB = 20 log10(SNR),

when SNR > 0.

This is a descriptive effect-to-noise ratio for the benchmark's two classes; it is not a power-ratio definition and is not used as a substitute for a hypothesis test.

## 5. Welch TVLA / two-sample t-test

For fixed and random trace sets with means x_bar_1, x_bar_2, sample variances s_1^2, s_2^2, and sample counts n_1, n_2, Welch's statistic is

t = (x_bar_1 - x_bar_2) /
    sqrt(s_1^2/n_1 + s_2^2/n_2).

The denominator allows unequal variances. The approximate Welch-Satterthwaite degrees of freedom are

nu =
(s_1^2/n_1 + s_2^2/n_2)^2 /
[
  (s_1^2/n_1)^2/(n_1-1) +
  (s_2^2/n_2)^2/(n_2-1)
].

The implementation computes a two-sided Student-t p-value

p = 2 P(T_nu >= |t|),

evaluated through the regularized incomplete beta function. Thus the report contains the actual t, |t|, nu, and p, rather than treating a fixed absolute-t threshold as the full statistical result.

### Reference threshold

The historical TVLA-style reference threshold |t| >= 4.5 is retained as a reference diagnostic for comparability with established leakage-evaluation practice. It is not represented as a universal significance threshold.

A threshold crossing and a p-value are reported separately. A TVLA test is a statistical leakage-detection screen, not proof that a system is secure or insecure. This limitation is emphasized in Standaert (2017).

The use of TVLA in side-channel evaluation is associated with fixed-vs-random trace testing and Welch's t-test; ISO/IEC 17825:2024 is the current ISO standard in this area. The previous ISO/IEC 17825:2016 edition is withdrawn.

## 6. Effect size: Cohen's d

Statistical significance can be driven by sample size and therefore is reported together with a standardized effect magnitude.

The pooled standard deviation is

s_p =
sqrt(
  ((n_1-1)s_1^2 + (n_2-1)s_2^2) /
  (n_1+n_2-2)
).

Cohen's d is then

d = (x_bar_1 - x_bar_2) / s_p.

The sign follows the order (fixed, random) used by the API. A negative value therefore means the fixed-class mean is smaller than the random-class mean.

For unequal variances, Welch's t-test remains the inferential procedure; Cohen's d is included as a standardized descriptive effect size and should not be treated as a variance-homogeneity assumption.

## 7. Multiple comparisons

The evaluation lab performs one TVLA comparison for each completed run and treats these run-level p-values as one multiplicity family.

### Bonferroni

For m hypotheses,

p_i_adj = min(1, m p_i).

This controls the family-wise error rate (FWER) under arbitrary dependence.

### Benjamini-Hochberg FDR

Sort the p-values so that

p_(1) <= ... <= p_(m).

The adjusted values are formed as

q_(i) = min_{j >= i} (m/j) p_(j),

with a final cap at 1 and the results mapped back to the original test order.

The default is Benjamini-Hochberg because the run-wise analysis is a screening family where controlling the expected false-discovery proportion is useful. Bonferroni remains available when strict FWER control is preferred.

A two-sided alpha=0.05 criterion is used for the significant field after the selected multiplicity correction. The corrected p-values and the historical |t|=4.5 diagnostic are both retained in the JSON report.

## 8. Repeatability: CV and ICC(A,1)

The existing coefficient of variation remains

CV(%) = 100 s / |x_bar|.

It is computed across the per-run fixed-class medians. CV describes relative dispersion but does not explicitly model agreement among repeated measurements.

The Evaluation Lab additionally reports the intraclass correlation coefficient ICC(A,1), following the two-way random-effects, single-measure, absolute-agreement formulation.

For a balanced matrix x_ij with n rows and k repeated measurements, define row means x_bar_i., column means x_bar_.j, and the grand mean x_bar_... Let

MS_R = [k/(n-1)] sum_i (x_bar_i. - x_bar_..)^2,

MS_C = [n/(k-1)] sum_j (x_bar_.j - x_bar_..)^2,

and

MS_E =
[
  sum_{i,j}
  (x_ij - x_bar_i. - x_bar_.j + x_bar_..)^2
] / [(n-1)(k-1)].

The implemented ICC(A,1) is

ICC(A,1) =
(MS_R - MS_E) /
[
  MS_R + (k-1)MS_E + (k/n)(MS_C - MS_E)
].

In the Evaluation Lab, rows correspond to aligned sample positions and columns correspond to independent measurement runs of the fixed-input class. This is a repeatability estimate for the measurement protocol across runs. It should not be described as evidence of between-day, between-machine, or population-level reliability because the current experiment does not sample those factors.

The choice and reporting of the ICC form follows recommendations to identify the model, type, and definition explicitly (McGraw & Wong, 1996; Koo & Li, 2016).

## 9. Reporting and interpretation

The JSON report records, at minimum:

- sample counts and alignment loss;
- primary outlier method and MAD-vs-IQR rejection percentages;
- latency means and medians with bootstrap 95% CIs;
- SNR and classification error;
- aggregate Welch t-statistic, Welch-Satterthwaite degrees of freedom, p-value, and Cohen's d;
- per-run TVLA p-values and multiplicity-adjusted p-values;
- CV and ICC(A,1);
- the retained |t|=4.5 reference diagnostic;
- platform metadata and the synthetic recovery check.

The report deliberately separates descriptive magnitude (SNR, Cohen's d, CV, ICC) from inferential quantities (p, adjusted p) and from the historical TVLA reference threshold.

## 10. Statistical caveats for the paper

The following points should be stated explicitly in a publication:

1. Timing samples from a microbenchmark can be temporally dependent. The ordinary bootstrap used here resamples individual observations and is therefore an approximation for dependent traces. A block bootstrap should be preferred when autocorrelation is substantial.
2. Welch's test assesses a mean difference at the analyzed sample position(s); it is not a proof of non-leakage when the null is not rejected.
3. Multiple-testing correction is defined over a pre-specified family. Adding unreported hypotheses after inspecting the results would invalidate the stated error-control claim.
4. Outlier rejection is part of the analysis pipeline and must be fixed before inspecting final results. Both MAD and IQR counts are retained to quantify sensitivity.
5. ICC is design-dependent. The ICC(A,1) estimate here is a protocol-repeatability statistic, not a generic reliability score for all CPUs or environments.
6. Confidence intervals quantify sampling uncertainty conditional on the measurement design. They do not include all sources of systematic uncertainty such as scheduler interference, NUMA placement, frequency scaling, thermal state, virtualization, or hardware-specific timer behavior.

## References

1. B. L. Welch, "The Generalization of ‘Student's’ Problem When Several Different Population Variances Are Involved," Biometrika, 34(1–2), 28–35, 1947. DOI: https://doi.org/10.1093/biomet/34.1-2.28
2. B. Efron, "Bootstrap Methods: Another Look at the Jackknife," The Annals of Statistics, 7(1), 1–26, 1979. DOI: https://doi.org/10.1214/aos/1176344552
3. Y. Benjamini and Y. Hochberg, "Controlling the False Discovery Rate: A Practical and Powerful Approach to Multiple Testing," Journal of the Royal Statistical Society: Series B, 57(1), 289–300, 1995. DOI: https://doi.org/10.1111/j.2517-6161.1995.tb02031.x
4. J. Cohen, Statistical Power Analysis for the Behavioral Sciences, 2nd ed., Routledge, 1988.
5. F. R. Hampel, "The Influence Curve and Its Role in Robust Estimation," Journal of the American Statistical Association, 69(346), 383–393, 1974. DOI: https://doi.org/10.1080/01621459.1974.10482962
6. J. W. Tukey, Exploratory Data Analysis, Addison-Wesley, 1977.
7. K. O. McGraw and S. P. Wong, "Forming Inferences About Some Intraclass Correlation Coefficients," Psychological Methods, 1(1), 30–46, 1996. DOI: https://doi.org/10.1037/1082-989X.1.1.30; see also the published erratum, DOI: https://doi.org/10.1037/1082-989X.1.4.390
8. T. K. Koo and M. Y. Li, "A Guideline of Selecting and Reporting Intraclass Correlation Coefficients for Reliability Research," Journal of Chiropractic Medicine, 15(2), 155–163, 2016. DOI: https://doi.org/10.1016/j.jcm.2016.02.012
9. ISO/IEC 17825:2024, Information technology — Security techniques — Testing methods for the mitigation of non-invasive attack classes against cryptographic modules, 2nd edition, 2024. https://www.iso.org/standard/82422.html
10. F.-X. Standaert, "How (not) to Use Welch's T-test in Side-Channel Security Evaluations," IACR Cryptology ePrint Archive, Report 2017/138, 2017. https://eprint.iacr.org/2017/138
