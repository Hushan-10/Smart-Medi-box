# Smart-Medi-box 










# Gemma‑4 E4B — Single‑Page Document Fine‑Tune
## Vision Tower **Frozen** vs **Trained** — Evaluation Report

**Model:** `google/gemma-4-E4B-it` · unsloth LoRA · bf16 **Date:** 2026‑08‑08
**Test set:** 47 held‑out documents across 5 classes (bank statement, driver's license, paystub, state ID, utility bill)
**Tasks:** single‑page **classify** + **extract**, instruction‑tuned · greedy (deterministic) decoding

---

> ### TL;DR
> Fine‑tuning lifts the base model from **28% → 100%** classification accuracy and **0.31 → 0.85** extraction F1 on the held‑out test set, with **zero pipeline failures**.
> Training the vision tower adds a **small, consistent extraction gain (+1.1 F1)** but costs **~38% more training time** and gives **no classification benefit**.
> **Recommendation: keep the vision tower frozen** — it matches the trained‑vision arm on classification and reaches ~99% of its extraction quality at ~72% of the training cost.

---

## 1. Experiment design

Two fine‑tunes were run under an identical protocol; **only one flag differs** between them:

| Held constant (both arms) | Value |
|---|---|
| Base model | `google/gemma-4-E4B-it` (unsloth `FastVisionModel`, bf16) |
| LoRA recipe | r=16, α=32, dropout=0.01, lr=2e‑4, 2 epochs, batch 1 × grad‑accum 8, seed 3407 |
| Data / split | `ca-dataset-model4` — 760 train / 94 val / 94 test records (474 docs), same split, same seed |
| Evaluation | same held‑out test split, same prompts, same client scoring conventions |

| The **only** variable | Arm A | Arm B |
|---|---|---|
| `finetune_vision_layers` | **False** (vision frozen) | **True** (vision trained) |

A third reference — the **un‑fine‑tuned base model** — was scored identically to establish the before/after floor.

---

## 2. Headline result — the fine‑tuning lift

![Base vs fine‑tuned — classification accuracy and extraction F1](before_after.png)

| Metric | Base (no FT) | Frozen | Vision | Best vs base |
|---|---:|---:|---:|---:|
| **Classification accuracy** | 0.277 | **1.000** | **1.000** | **+0.723** (3.6×) |
| Classification macro‑F1 | 0.358 | 1.000 | 1.000 | +0.642 |
| **Extraction F1** | 0.310 | 0.838 | **0.849** | **+0.538** (2.7×) |
| Extraction precision | 0.493 | 0.857 | 0.871 | +0.378 |
| Extraction recall | 0.226 | 0.819 | 0.828 | +0.601 (3.7×) |
| Fields matched | 67 / 365 | 263 / 365 | **269 / 365** | +202 fields |
| Mean per‑doc score | 0.198 | 0.768 | 0.784 | +0.587 |
| Pipeline failures | 0 | 0 | 0 | — |

**Both fine‑tuned models classify every one of the 47 test documents correctly** and extract ~85% of fields, versus a base model that misreads ~3 of every 4 documents.

---

## 3. The A/B question — does training the vision tower help?

![Frozen vs vision — metric bars](metric_bars.png)

| Metric | Frozen | Vision | Δ (vision − frozen) |
|---|---:|---:|---:|
| Classification accuracy | 1.000 | 1.000 | **0.000** (tie) |
| Extraction precision | 0.857 | 0.871 | +0.014 |
| Extraction recall | 0.819 | 0.828 | +0.008 |
| **Extraction F1** | 0.838 | **0.849** | **+0.011** |
| Fields matched | 263 | 269 | +6 |
| Mean per‑doc score | 0.768 | 0.784 | +0.017 |

**Read‑out:** the vision arm is better on *every* extraction metric, but by a small margin (**+1.1 F1 points, +6 of 365 fields**). Classification is already saturated at 100% for both, so the vision tower contributes nothing there.

### Cost of that gain

| Cost dimension | Frozen | Vision | Overhead |
|---|---:|---:|---:|
| Training time | 28.3 min | 39.2 min | **+38%** |
| Trainable params | 36.7 M | 41.3 M | +12.5% |
| Peak VRAM | 34.1 GB | 34.4 GB | +0.7% |

The extra ~4.6 M trainable parameters (the vision‑tower LoRA) buy a real but marginal extraction improvement at a meaningfully higher training cost, and no classification benefit.

---

## 4. Training dynamics

![Frozen vs vision — training and validation loss](loss_compare.png)

Both arms converge cleanly: validation loss drops sharply, then **plateaus at ~0.0166 with no upward turn** — i.e. **no overfitting** at 2 epochs. The two curves are essentially indistinguishable (`final_eval_loss` = 0.0166 for both), which is why the test‑set differences are small. This is a good example of why the **held‑out task metrics — not the loss curve — are the deciding evidence.**

---

## 5. Per‑class results

**Classification** — perfect for every class in both fine‑tuned arms:

| Class | Support | Base F1 | Frozen F1 | Vision F1 |
|---|---:|---:|---:|---:|
| bank_statement | 10 | 0.56 | **1.00** | **1.00** |
| drivers_license | 9 | 0.50 | **1.00** | **1.00** |
| paystub | 9 | 0.20 | **1.00** | **1.00** |
| state_id | 10 | 0.33 | **1.00** | **1.00** |
| utility_bill | 9 | 0.20 | **1.00** | **1.00** |

**Extraction, by class** (vision arm — field‑level micro‑F1):

| Class | Precision | Recall | F1 |
|---|---:|---:|---:|
| drivers_license | 1.00 | 1.00 | **1.00** |
| state_id | 0.93 | 0.91 | **0.92** |
| utility_bill | 0.87 | 0.96 | **0.91** |
| bank_statement | 0.76 | 1.00 | **0.87** |
| **paystub** | 0.85 | 0.51 | **0.63** ← weakest |

Driver's‑license and state‑ID extraction is near‑perfect. **Paystub is the one class dragging the overall number down**, driven by low recall.

---

## 6. Where extraction still struggles

The remaining ~15% extraction gap is **concentrated in a handful of paystub fields**, not spread evenly:

| Class · field | TP | FP | FN | F1 | Failure pattern |
|---|---:|---:|---:|---:|---|
| paystub · direct_deposit_bank_type | 0 | 0 | 9 | **0.00** | never returned |
| paystub · job_hire_date | 0 | 0 | 9 | **0.00** | never returned |
| paystub · pay_period_start | 1 | 1 | 7 | 0.20 | date rarely read |
| paystub · pay_frequency | 2 | 1 | 6 | 0.36 | low recall |
| paystub · pay_period_end | 3 | 0 | 6 | 0.50 | date rarely read |
| paystub · medicare_tax | 4 | 0 | 5 | 0.62 | low recall |
| bank_statement · bank_account_type | 5 | 5 | 0 | 0.67 | over‑predicts (precision) |

**Takeaway for the next iteration:** paystub temporal fields (hire date, pay‑period start/end, pay frequency) and the direct‑deposit fields are the highest‑value targets — likely under‑represented or inconsistently printed in the training data. Two fields (`direct_deposit_bank_type`, `job_hire_date`) are returned **zero** times, which suggests they are absent from most training pages rather than merely hard to read.

---

## 7. Recommendation

**Ship the vision‑frozen model.** It:
- matches the vision‑trained arm exactly on classification (100%),
- reaches **98.7%** of its extraction F1 (0.838 vs 0.849),
- and trains in **~72%** of the time with fewer parameters.

The vision‑trained arm is the marginally stronger model *if* extraction F1 is the single overriding metric and training compute is not a constraint — but the +1.1 F1 gain does not justify the +38% cost for routine retraining.

**Higher‑leverage next step than un‑freezing vision:** improve **paystub** coverage — add/curate training examples that clearly print pay‑period dates, pay frequency, hire date, and direct‑deposit details. That targets the actual bottleneck (Section 6) and would lift the headline extraction number more than the vision tower did.

---

## Appendix — reproducibility & methodology parity

- **Recipe/prompt/scoring parity:** both notebooks replicate the jac pipeline (`finetune/training/configs/e4b_unsloth_bf16.toml`) and the Databricks reference notebooks — identical LoRA target flags, hyperparameters, seed, prompts (the dataset itself was built by the jac pipeline), and the client scoring conventions (numeric within 0.01, ISO dates exact, else `token_set_ratio ≥ 85`; `transactions`/`balance_reconciliation` auto‑skipped; denominator = every schema field including empties).
- **One scoring nuance to note:** the Python evaluators assign each field to exactly one bucket (TP / FN / FP), matching the Databricks eval bit‑for‑bit. The jac harness counts a wrong‑value field as *both* FP and FN. Consequently **classification metrics, per‑doc score, and mean‑doc score match the jac harness exactly**, while extraction precision/recall/F1 are directly comparable to the Databricks notebooks and *substantively* (not bit‑for‑bit) comparable to the jac harness.
- **Determinism:** greedy decoding (`do_sample=False`), fixed seed 3407 — single‑run numbers are reproducible.

### Artifact index
| File | Contents |
|---|---|
| `comparison.csv` | machine‑readable version of the tables above |
| `before_after.png`, `metric_bars.png`, `loss_compare.png` | figures used in this report |
| `e4b-singlepage-frozen-…/eval_*.xlsx` | frozen arm — full per‑doc / per‑field / confusion sheets |
| `e4b-singlepage-vision-…/eval_*.xlsx` | vision arm — full per‑doc / per‑field / confusion sheets |
| `base-gemma-4-E4B-it/eval_*.xlsx` | base baseline — full detail |
| `*/summary.json`, `*/predictions.json` | raw metrics and kept model outputs (re‑scoreable) |





