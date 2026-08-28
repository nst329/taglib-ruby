# frozen-string-literal: true

source 'http://rubygems.org'

# Use dependencies from .gemspec
gemspec

# kramdown is only needed by YARD's Markdown handling in GitHub Actions.
gem 'kramdown', '~> 2.3.0' if ENV['GITHUB_ACTIONS'] == 'true'
