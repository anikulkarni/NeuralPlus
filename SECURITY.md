# Security policy

## Supported versions

NeuralPlus is currently pre-alpha. Security fixes will be applied to the latest development branch until versioned releases begin.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability, credential leak, or unsafe default.

Use GitHub's private vulnerability reporting feature for this repository when available. If private reporting is not available, contact the repository owner privately through the contact method listed on the GitHub profile.

Include:

- Affected commit or version
- Reproduction steps
- Security impact
- Whether credentials or personal data are involved
- A proposed fix, when available

## Security principles

Provider and tracing implementations must:

- Never log API keys, authorization headers, or complete credential objects
- Redact sensitive values from exceptions and fixtures
- Validate tool names and arguments before execution
- Make trace-content capture explicit and configurable
- Use secure transport verification by default
- Avoid executing model-generated shell commands unless an application explicitly installs such a tool

The project is not yet audited. Consumers are responsible for reviewing it before security-sensitive deployment.
