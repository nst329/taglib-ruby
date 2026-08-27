# frozen-string-literal: true

require 'taglib_mp4'

module TagLib::MP4
  # Immutable Ruby value copied to and from TagLib's MP4::Chapter.
  class Chapter
    attr_reader :start_time, :title

    def initialize(start_time = nil, title = nil, **keywords)
      if keywords.any?
        raise ArgumentError, 'use either positional or keyword arguments' unless start_time.nil? && title.nil?

        start_time = keywords.delete(:start_time)
        title = keywords.delete(:title)
        raise ArgumentError, "unknown keywords: #{keywords.keys.join(', ')}" unless keywords.empty?
      end
      raise ArgumentError, 'start_time is required' if start_time.nil?
      raise ArgumentError, 'title is required' if title.nil?
      raise ArgumentError, 'start_time must be an Integer' unless start_time.is_a?(Integer)
      raise ArgumentError, 'start_time must not be negative' if start_time.negative?
      raise ArgumentError, 'title must be a UTF-8 String' unless title.is_a?(String) && title.encoding == Encoding::UTF_8 && title.valid_encoding?
      raise ArgumentError, 'title must not contain NUL bytes' if title.include?("\0")

      @start_time = start_time
      @title = title
      freeze
    end

    def ==(other)
      other.is_a?(Chapter) && start_time == other.start_time && title == other.title
    end
    alias eql? ==

    def hash
      [start_time, title].hash
    end

    def inspect
      format('#<%s start_time=%d title=%p>', self.class, start_time, title)
    end
  end

  class ChapterConflictError < StandardError; end
  class ChapterSaveError < StandardError; end

  class File
    extend ::TagLib::FileOpenable

    CHAPTER_STYLE_CODES = {
      nero: 1,
      quicktime: 2,
      both: 3,
      any: 4
    }.freeze

    def chapters(style: nil)
      read_chapter_style_code(style) unless style.nil?
      if @chapter_state
        return chapter_state_chapters(style)
      end
      code = style.nil? ? CHAPTER_STYLE_CODES[:any] : CHAPTER_STYLE_CODES.fetch(style)
      _chapters(code)
    end

    def chapter_style
      return chapter_state_style if @chapter_state

      _chapter_style
    end

    def set_chapters(chapters, style: :preserve)
      ensure_chapter_state
      if style == :preserve
        style = chapter_style
        style = :both if style == :none
      end
      _set_chapters(chapters, chapter_style_code(style))
      update_chapter_state(chapters, style)
      self
    end

    def remove_chapters(style: :both)
      ensure_chapter_state
      if style == :preserve
        style = chapter_style
        style = :both if style == :none
      end
      _remove_chapters(chapter_style_code(style))
      update_chapter_state([], style)
      self
    end

    def nero_chapters
      chapters(style: :nero)
    end

    def set_nero_chapters(chapters)
      set_chapters(chapters, style: :nero)
    end

    def remove_nero_chapters
      remove_chapters(style: :nero)
    end

    def quicktime_chapters
      chapters(style: :quicktime)
    end

    def set_quicktime_chapters(chapters)
      set_chapters(chapters, style: :quicktime)
    end

    def remove_quicktime_chapters
      remove_chapters(style: :quicktime)
    end

    def save_chapters
      result = _save_chapters
      @chapter_state = nil
      result
    end

    alias save_without_chapter_state save
    private :save_without_chapter_state

    def save(*args)
      result = save_without_chapter_state(*args)
      @chapter_state = nil if result
      result
    end

    private

    def chapter_style_code(style)
      CHAPTER_STYLE_CODES.fetch(style) do
        raise ArgumentError, "invalid chapter style: #{style.inspect}"
      end
    end

    def read_chapter_style_code(style)
      case style
      when :nero, :quicktime
        chapter_style_code(style)
      else
        raise ArgumentError, "invalid chapter read style: #{style.inspect}"
      end
    end

    def ensure_chapter_state
      return if @chapter_state

      @chapter_state = {
        nero: _chapters(CHAPTER_STYLE_CODES[:nero]),
        quicktime: _chapters(CHAPTER_STYLE_CODES[:quicktime])
      }
    end

    def update_chapter_state(chapters, style)
      values = Array(chapters).dup.freeze
      @chapter_state[:nero] = values if style == :nero || style == :both
      @chapter_state[:quicktime] = values if style == :quicktime || style == :both
    end

    def chapter_state_style
      nero = !@chapter_state[:nero].empty?
      quicktime = !@chapter_state[:quicktime].empty?
      return :both if nero && quicktime
      return :nero if nero
      return :quicktime if quicktime

      :none
    end

    def chapter_state_chapters(style)
      return @chapter_state[:nero].dup if style == :nero
      return @chapter_state[:quicktime].dup if style == :quicktime

      nero = @chapter_state[:nero]
      quicktime = @chapter_state[:quicktime]
      if !nero.empty? && !quicktime.empty? && nero != quicktime
        raise ChapterConflictError, 'Nero and QuickTime chapters differ'
      end
      (nero.empty? ? quicktime : nero).dup
    end
  end

  class Tag
    remove_method :save
  end

  class Item
    def self.from_int_pair(ary)
      raise ArgumentError, 'argument should be an array' unless ary.is_a? Array
      raise ArgumentError, 'argument should have exactly two elements' if ary.length != 2

      new(*ary)
    end
  end

  class ItemMap
    alias clear _clear
    alias insert _insert
    alias [] fetch
    alias []= insert
    remove_method :_clear
    remove_method :_insert
  end
end
