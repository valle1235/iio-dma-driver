#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/slab.h>

#define FFT_BUFFER_SIZE (1024 * 2)  // 1024 samples, 16-bit each (eller var det 32-bit styck?)

struct fft_iio_dev {
	struct dma_chan *dma_chan;
	dma_addr_t dma_phys;
	void *dma_buf;
	struct device *dev;
};

static int fft_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	return -EOPNOTSUPP;
}

static irqreturn_t fft_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct fft_iio_dev *st = iio_priv(indio_dev);

	iio_push_to_buffers_with_timestamp(indio_dev, st->dma_buf, iio_get_time_ns(indio_dev));
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int fft_buffer_postenable(struct iio_dev *indio_dev)
{
	struct fft_iio_dev *st = iio_priv(indio_dev);
	struct dma_async_tx_descriptor *desc;

	desc = dmaengine_prep_slave_single(st->dma_chan, st->dma_phys,
					   FFT_BUFFER_SIZE, DMA_DEV_TO_MEM, 0);
	if (!desc)
		return -EIO;

	desc->callback = NULL;
	dmaengine_submit(desc);
	dma_async_issue_pending(st->dma_chan);
	return 0;
}

static int fft_buffer_predisable(struct iio_dev *indio_dev)
{
	struct fft_iio_dev *st = iio_priv(indio_dev);
	dmaengine_terminate_all(st->dma_chan);
	return 0;
}

static const struct iio_buffer_setup_ops fft_buffer_ops = {
	.postenable = fft_buffer_postenable,
	.predisable = fft_buffer_predisable,
};

static const struct iio_chan_spec fft_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
};

static const struct iio_info fft_iio_info = {
	.read_raw = fft_read_raw,
};

static int fft_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct fft_iio_dev *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->dev = &pdev->dev;

	st->dma_chan = dma_request_chan(&pdev->dev, "fft_dma");
	if (IS_ERR(st->dma_chan))
		return dev_err_probe(&pdev->dev, PTR_ERR(st->dma_chan), "Failed to request DMA\n");

	st->dma_buf = dma_alloc_coherent(&pdev->dev, FFT_BUFFER_SIZE, &st->dma_phys, GFP_KERNEL);
	if (!st->dma_buf)
		return -ENOMEM;

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = "fft_iio";
	indio_dev->info = &fft_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = fft_channels;
	indio_dev->num_channels = ARRAY_SIZE(fft_channels);

	iio_buffer_setup(indio_dev, &fft_buffer_ops);

	ret = devm_iio_triggered_buffer_setup(&pdev->dev, indio_dev, NULL,
						fft_trigger_handler, &fft_buffer_ops);
	if (ret)
		return ret;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static const struct of_device_id fft_of_match[] = {
	{ .compatible = "custom,axi-fft" },
	{},
};
MODULE_DEVICE_TABLE(of, fft_of_match);

static struct platform_driver fft_driver = {
	.probe = fft_probe,
	.driver = {
		.name = "fft_iio",
		.of_match_table = fft_of_match,
	},
};
module_platform_driver(fft_driver);

MODULE_AUTHOR("Andreas Söderlund");
MODULE_DESCRIPTION("DMA driver for FFT");
MODULE_LICENSE("GPL");
