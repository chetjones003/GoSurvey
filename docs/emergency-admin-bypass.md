# Emergency repository-admin bypass

The repository admin may explicitly direct an emergency push or merge to `beta` or `master` without
the normal pull request review and required checks. This is an admin decision; the usual branch
workflow remains the default for all other work.

## How the bypass works

GitHub's branch protection is configured with administrator enforcement disabled on both `master`
and `beta`. A repository admin can therefore bypass the protected-branch requirements when GitHub
offers the bypass action. The current protections otherwise require a pull request, one approval,
conversation resolution, and the configured status checks. No repository settings need to be
changed to use the existing admin bypass.

If GitHub does not offer the bypass action, stop and report the specific protection that blocks the
requested action. Do not weaken repository settings as a workaround without an explicit instruction.

## Record the emergency action

After a direct push or bypassed merge, create a follow-up pull request or issue when practical. Record:

- the commit or pull request that used the bypass;
- why the admin chose the emergency path;
- which review or checks were skipped;
- what validation was completed before or after the action; and
- any follow-up fixes or release steps.

An explicit instruction from the repository admin is required before an agent uses this bypass.
Urgency by itself is not authorization.
